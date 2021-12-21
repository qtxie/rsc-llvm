/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "Tokenizer.h"
#include "Compilation.h"

static bool in_range(uint32_t *range, uint32_t c) {
  for (int i = 0; range[i] != -1; i += 2)
    if (range[i] <= c && c <= range[i + 1])
      return true;
  return false;
}

// This function returns true if a given character is acceptable as
// the first character of an identifier.
//
// The following characters are not allowed:
//   whitespaces control-chars /\,[](){}"#%$@:;'
//
// NOTE!!! We accept all the codepoints in range [0x00A8, 0x2FA1F], this is not safe.
static bool isSymbolChar(uint32_t c) {
  static uint32_t range[] = {
    '^', 'z', 'A', 'Z', '0', '9', '=', '?', '*', '+', '-', '.', 
    '!', '!', '|', '|', '~', '~', 0x00A8, 0x2FA1F, -1,
  };
  return in_range(range, c);
}

// The following characters are not allowed:
//   whitespaces control-chars [](){}"@:;
//
static bool isFileChar(uint32_t c) {
  static uint32_t range[] = {
    '^', 'z', 'A', 'Z', '*', '9', '<', '?', '#', '\'', '|', '|', '~', '~',
    '!', '!', '\\', '\\', '|', '|', 0x00A8, 0x2FA1F, -1,
  };
  return in_range(range, c);
}

uint32_t Tokenizer::decodeUtf8(const char *p, const char **new_pos) {
    if ((unsigned char)*p < 128) {
        *new_pos = p + 1;
        return *p;
    }

    const char *start = p;
    int len;
    uint32_t c;

    if ((unsigned char)*p >= 0b11110000) {
        len = 4;
        c = *p & 0b111;
    } else if ((unsigned char)*p >= 0b11100000) {
        len = 3;
        c = *p & 0b1111;
    } else if ((unsigned char)*p >= 0b11000000) {
        len = 2;
        c = *p & 0b11111;
    } else {
        tokenError(p, err_invalid_utf8);
    }

    for (int i = 1; i < len; i++) {
        if ((unsigned char)p[i] >> 6 != 0b10)
            tokenError(p, err_invalid_utf8);
        c = (c << 6) | (p[i] & 0b111111);
    }

    *new_pos = p + len;
    return c;
}

int Tokenizer::scanSymbol(const char *p) {
    const char *start = p;
    for (;;) {
        const char *q;
        uint32_t c = decodeUtf8(p, &q);
        if (!isSymbolChar(c))
            return p - start;
        p = q;
    }
}

const char * Tokenizer::scanBinary(const char *p) {
    return p;
}

const char * Tokenizer::scanByte(const char *p) {
    return p;
}

int Tokenizer::scanString(const char *p) {
    return 0;
}

int Tokenizer::scanFile(const char *p) {
    if (*p == '%') {
        p++;
        if (*p == '"') {
            return scanString(p);
        } else {
            const char *start = p;
            for (;;) {
                const char *q;
                uint32_t c = decodeUtf8(p, &q);
                if (!isFileChar(c))
                    return p - start;
                p = q;
            }
        }
    } else {
        tokenError(p, err_invalid_token);
        return 0;
    }
}

Token * Tokenizer::scanDirectives(const char *p) {
    const static std::array<std::pair<StringRef, TokenId>, 14> directives = {{
        {"include", TokenIdMacroInclude}, {"define", TokenIdMacroDefine},
        {"import", TokenIdMacroImport},   {"syscall", TokenIdMacroSyscall},
        {"enum", TokenIdMacroEnum},       {"if", TokenIdMacroIf},
        {"either", TokenIdMacroEither},   {"switch", TokenIdMacroSwitch},
        {"case", TokenIdMacroCase},       {"verbose", TokenIdMacroVerbose},
        {"u16", TokenIdMacroU16},         {"call", TokenIdMacroCall},
        {"export", TokenIdMacroExport},   {"inline", TokenIdMacroInline}
    }};

    int n = scanSymbol(p);
    if (n == 0) {tokenError(p, err_invalid_token);}

    StringRef sym(p, n);
    Token *tok = nullptr;
    for (auto D : directives) {
        if (sym.equals(D.first)) {
            tok = newToken(ctx.TokBuf, D.second, p, n);
            break;
        }
    }
    
    // To get better cache performance, we process the include file here
    // so we can put all the tokens in memory sequentially
    // For non-trivial cases, e.g include file in macros
    // We let the preprocessor handle them.
    if (tok) {
        if (tok->id == TokenIdMacroInclude) {
            p += n;
            while (isspace(*p)) p++;
            n = scanFile(p);
            if (n > 0) {
                const char * buf = ctx.readFile(std::string(p + 1, n));
                tok = tokenize(buf);
            }
        }
        return tok;
    } else {
        return tokenError(p, err_unknown_directive);
    }
}

Token * Tokenizer::tokenize(const char *p) {
    Token head = {};
    Token *cur = &head;

    // Skip the UTF-8 "BOM" marker
    if (!memcmp(p, "\xef\xbb\xbf", 3)) {
        p += 3;
    }

    while (*p) {
        // Skip line comments
        if (*p == ';') {
            // NOTE: we don't bother with \r as the line break as it's rarely used now
            // OSX, Linux: \n
            // Windows: \r\n
            while (*p != '\n') p++;
            continue;
        }

        // Skip whitespace characters
        // ' ','\t','\n','\r','\v','\f'
        if (isspace(*p)) {
            p++;
            continue;
        }

        // Directives
        if (*p == '#') {
            p++;
            if (*p == '{') {           // Binary arrays
                p = scanBinary(p);
            }
            else if (*p == '"') {
                p = scanByte(p);
            }
            else {
                cur = cur->next = scanDirectives(p); 
                p += cur->len;
            }
            continue;
        }

        // Delimiters
        switch (*p)
        {
        #define CASE(ch, tok_id)                                       \
            case ch:                                                   \
                cur = cur->next = newToken(ctx.TokBuf, tok_id, p, 1);  \
                p++;                                                   \
                break

        CASE('[', TokenIdLBracket);
        CASE(']', TokenIdRBracket);
        CASE('(', TokenIdLBrace);
        CASE(')', TokenIdRBrace);
        
        default:
            break;
        }

        // Numeric literals
        // E.g. 123  1e3  1.2  .12  ,12  123'45  123'45E+2  1,23e-2
        if (isdigit(*p) || ((*p == '.' || *p == ',') && isdigit(p[1]))) {
            TokenId id = isdigit(*p) ? TokenIdIntLiteral : TokenIdFloatLiteral;
            bool has_dot = (id == TokenIdFloatLiteral);
            bool has_exp = false;
            const char *q = p++;
            while (!isDelimiter(*p)) {
                if (isdigit(*p) || *p == '\'') {
                    p++;
                } else if (*p == 'e' || *p == 'E') {
                    if (has_exp) tokenError(p, err_invalid_token);
                    has_exp = true;
                    id = TokenIdFloatLiteral;
                    p++;
                    if (*p == '-' || *p == '+') p++;
                } else if (*p == '.' || *p == ',') {
                    if (has_exp || has_dot)
                        tokenError(p, err_invalid_token);
                    has_dot = true;
                    id = TokenIdFloatLiteral;
                    p++;
                } else tokenError(p, err_invalid_token);
            }
            cur = cur->next = newToken(ctx.TokBuf, id, q, p - q);
            continue;
        }

        // Any words
        tokenError(p, err_invalid_token);
    }

    cur = cur->next = newToken(ctx.TokBuf, TokenIdEOF, p, 0);
    return head.next;
}
