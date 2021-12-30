/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#define USE_UNICODE

#include "LexMap.h"
#include "Tokenizer.h"
#include "Compilation.h"

static bool in_range(uint32_t *range, uint32_t c) {
    for (int i = 0; range[i] != -1; i += 2)
        if (range[i] <= c && c <= range[i + 1])
        return true;
    return false;
}

// The following characters are not allowed:
//   whitespaces control-chars [](){}"@:;
//
static bool isFileChar(uint32_t c) {
    static uint32_t range[] = {
        '^', 'z', 'A', 'Z', '*', '9', '<', '?', '#', '\'', '|', '|', '~', '~',
        '!', '!', '\\', '\\', '|', '|', 0x00A8, 0x2FA1F, -1
    };
    return in_range(range, c);
}

static bool isHexChar(int c) {
    static uint32_t range [] = {'0', '9', 'A', 'F', -1};
    return in_range(range, c);
}

// Skip whitespaces and line comments
//
static const char * skipSpaces(const char *p) {
    for (;;) {
        while (IS_LEX_ANY_SPACE(*p)) p++;
        if (*p == ';')
            while (*p != '\n') p++;
        else
            break;
    }
    return p;
}

constexpr auto genKeywordsTable()
{
    enum _KEYWORD_ {
        #define KEYWORD(ID, STR_LOWER) _Keyword ## ID,
        #include "TokenId.def"
        KeywordCount
    };
    std::array<std::pair<StringRef, TokenId>, KeywordCount> keywords = {{
        #define KEYWORD(ID, STR_LOWER) {STR_LOWER, TokenId ## ID},
        #include "TokenId.def"
    }};
    return keywords;
}

static TokenId getSymbolId(const char *p, int len) {
    static constexpr auto kw_table = genKeywordsTable();

    StringRef sym(p, len);

    auto it = std::lower_bound(kw_table.begin(), kw_table.end(), sym,
        [](const auto &a, const auto &b) {return -1 == a.first.compare_insensitive(b);});

    if (it == kw_table.end() || it->first != sym) {
        return TokenIdWord;
    } else {
        return it->second;
    }
}

constexpr auto genDirectivesTable()
{
    enum _MACRO_ {
        #define MACRO(ID, STR_LOWER) _MACRO ## ID,
        #include "TokenId.def"
        MacroCount
    };
    std::array<std::pair<StringRef, TokenId>, MacroCount> keywords = {{
        #define MACRO(ID, STR_LOWER) {STR_LOWER, TokenIdMacro ## ID},
        #include "TokenId.def"
    }};
    return keywords;
}

Token * Tokenizer::scanDirectives(const char *p) {
    static constexpr auto directives = genDirectivesTable();

    int n = scanSymbol(p);
    if (n == 0) return tokenError(p, err_invalid_token);

    StringRef sym(p, n);
    auto it = std::lower_bound(directives.begin(), directives.end(), sym,
        [](const auto &a, const auto &b) {return -1 == a.first.compare(b);});

    if (it == directives.end() || it->first != sym) {
        return newToken(ctx.TokBuf, TokenIdIssue, p, n);
    } else {
        auto tok = newToken(ctx.TokBuf, it->second, p, n);
        tok->kind = TokenIdDirective;
        return tok;
    }
}

int Tokenizer::scanSymbol(const char *p) {
    const char *start = p;
    while(IS_LEX_AT_LEAST_WORD(*p)) p++;
    return p - start;
}

int Tokenizer::scanBinary(const char *p) {
    const char * start = p;
    while (*p != '}') {
        p = skipSpaces(p);
        if (isHexChar(*p) && isHexChar(p[1]))
            p += 2;
        else
            if (*p != '}') tokenError(p, err_invalid_token);
    }
    return p - start;
}

int Tokenizer::scanByte(const char *p) {
    int n = scanString(p);
    if (n == 1 && *p == '^' && p[2] == '"') {  // #"^""
        return 2;
    } else if ((n == 1 && *p != '^') || (n > 1 && n < 8 && *p == '^')) {
        return n;
    } else {
        tokenError(p, err_invalid_token);
    }
}

int Tokenizer::scanMultilineString(const char *p) {
    const char * start = p;
    if (p = strchr(p, '}'); p)
        return p - start;
    else
        tokenError(p, err_invalid_token);
}

int Tokenizer::scanString(const char *p) {
    const char * start = p;
    while (*p != '"') {
        if (*p == '\n' || *p == '\0') tokenError(p, err_invalid_token);
        p++;
    }
    return p - start;
}

int Tokenizer::scanFile(const char *p) {
    if (*p == '%') {
        p++;
        if (*p == '"') {
            return scanString(p + 1);
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
        return 0;
    }
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

#define ADD_TOK(ID, LOC, LEN) \
    cur = cur->next = newToken(ctx.TokBuf, ID, LOC, LEN); \
    p += LEN;

Token * Tokenizer::tokenize(const char *p, Token **end_token) {
    Token head = {};
    Token *cur = &head;

    // Skip the UTF-8 "BOM" marker
    if (!memcmp(p, "\xef\xbb\xbf", 3)) p += 3;

    while (*p) {
        bool has_space = false;
        bool has_sign  = false;
        bool has_apostrophe = false;

        // Skip whitespace characters
        // 
        while (IS_LEX_ANY_SPACE(*p)) {
            p++;
            has_space = true;
        }

        switch (*p)
        {
        #define CASE(ch, tok_id)        \
            case ch:                    \
                ADD_TOK(tok_id, p, 1);  \
                continue;

        CASE(']', TokenIdRBracket);
        CASE(')', TokenIdRParen);

        case '(':
            ADD_TOK(TokenIdLParen, p, 1);
            goto additional_check;

        case '[':
            ADD_TOK(TokenIdLBracket, p, 1);
        additional_check:
            has_space = true;
            if (*p == ':') goto case_colon;
            if (*p == '/') goto case_slash;
            continue;

        case ':': {
        case_colon:
            TokenId id = has_space ? TokenIdGetColon : TokenIdSetColon;
            if (
                (id == TokenIdGetColon && !IS_LEX_WORD(p[1])) ||
                (id == TokenIdSetColon && !IS_LEX_DELIMIT(p[1]))
            ){
                tokenError(p, err_invalid_token);
            } else {
                ADD_TOK(id, p, 1);
            }}
            continue;

        case '/':
        case_slash:
            if (has_space) {
                if (IS_LEX_DELIMIT(p[1])) {
                    ADD_TOK(TokenIdDiv, p, 1);
                }
                else if (p[1] == '/' && IS_LEX_DELIMIT(p[2])) {
                    ADD_TOK(TokenIdModulo, p, 2);
                } else {   // Refinement
                    p++;
                    int len = scanSymbol(p);
                    if (len) {
                        ADD_TOK(TokenIdRefinement, p, len);
                    } else {
                        tokenError(p, err_invalid_token);
                    }
                }
            } else {
                if (Lex_Map[p[1]] > LEX_DELIMIT_SEMICOLON) {
                    ADD_TOK(TokenIdSlash, p, 1);
                    if (*p == ':') {
                        ADD_TOK(TokenIdGetColon, p, 1);
                    }
                } else {  // e.g abc/ ef
                    tokenError(p, err_invalid_token);
                }
            }
            continue;

        case '+': case '-': {
            auto id = (*p == '+') ? TokenIdPlus : TokenIdMinus;
            if (IS_LEX_DELIMIT(p[1])) {
                ADD_TOK(id, p, 1);
                continue;
            } else if (IS_LEX_NUMBER(p[1])) {     // follows a number
                has_sign = true;
                p++;
                break;
            } else {
                break;
            }}

        case '%':
            if (IS_LEX_DELIMIT(p[1])) {
                ADD_TOK(TokenIdRemainder, p, 1);
            } else {
                int len = scanFile(p);
                p++;    // Skip %
                if (len > 0) {
                    bool is_quoted = (*p == '"');
                    if (is_quoted) p++;
                    ADD_TOK(TokenIdFile, p, len);
                    if (is_quoted) p++;
                } else {
                    tokenError(p, err_invalid_token);
                }
            }
            continue;

        case '\'':
            has_apostrophe = true;
            p++;
            break;

        case '~': {  // Special case: Red generates many words start with it.
            int n = 1;
            while (!IS_LEX_DELIMIT(p[n])) n++;
            auto id = (p[n - 1] == ':') ? TokenIdSetWord : TokenIdWord;
            ADD_TOK(id, p, n);
            }
            continue;

        case ';':
            // Skip line comments
            // NOTE: we don't bother with \r as the line break as it's rarely used now
            // OSX, Linux: \n
            // Windows: \r\n
            while (*p != '\n') p++;
            continue;

        case '"': {
            p++;    // Skip beginning "
            int len = scanString(p);
            ADD_TOK(TokenIdString, p, len);
            p++;    // Skip ending "
            }
            continue;

        case '{': {
            p++;
            int len = scanMultilineString(p);
            ADD_TOK(TokenIdMultilineString, p, len);
            p++;
            }
            continue;

        case '#':
            p++;
            if (*p == '{') {           // Binary arrays
                p++;
                int n = scanBinary(p);
                ADD_TOK(TokenIdBinary, p, n)
                p++;    // skip '}'
            }
            else if (*p == '"') {
                p++;
                int n = scanByte(p);
                ADD_TOK(TokenIdByte, p, n)
                p++;    // skip '"'
            }
            else {
                Token *tok = scanDirectives(p);
                p += tok->len;

                // We process the include file here to get better cache performance
                // so that we can put all the tokens in memory sequentially
                // For non-trivial cases, e.g include file in macros
                // We let the preprocessor handle them.
                if (tok->id == TokenIdMacroInclude) {
                    p = skipSpaces(p);
                    int n = scanFile(p);
                    if (n > 0) {
                        p++;    // skip %
                        bool is_quoted = (*p == '"');
                        if (is_quoted) p++;
                        Token *tk_end = nullptr;
                        tok = ctx.tokenizeFile(ctx.toNativePath(p, n), &tk_end, true);
                        cur->next = tok;
                        cur = tk_end;
                        p += n;
                        if (is_quoted) p++;
                    } else {
                        return tokenError(p, err_expected, "file");
                    }
                } else {
                    cur = cur->next = tok;
                }
            }
            continue;
        default:
            break;
        }  

        // Hexadecimal integer
        // Rule: 2 8 hexa e: #"h"
        // 04D2h AABBh 01h FFFFFFFFh
        int len = 0;
        while (isHexChar(p[len])) len++;
        if (len && p[len] == 'h') {
            if (len > 1 && len <= 8) {
                ADD_TOK(TokenIdHex, p, len);
                p++;    // skip 'h'
                continue;
            } else {
                tokenError(p, err_invalid_token);
            }
        }

        // Numeric literals
        // E.g. 123  1e3  1.2  .12  ,12  123'45  123'45E+2  1,23e-2
        if (IS_LEX_NUMBER(*p) || ((*p == '.' || *p == ',') && IS_LEX_NUMBER(p[1]))) {
            static constexpr StringLiteral inf("1.#inf");
            static constexpr StringLiteral nan("1.#nan");
            StringRef s(p, 6);
            if (
                (0 == inf.compare_insensitive(s) || 0 == nan.compare_insensitive(s)) && IS_LEX_DELIMIT(p[6])
            ) {
                ADD_TOK(TokenIdFloat, p, 6);
                continue;
            }

            TokenId id = IS_LEX_NUMBER(*p) ? TokenIdInteger : TokenIdFloat;
            bool has_dot = (id == TokenIdFloat);
            bool has_exp = false;
            const char *start = p++;
            while (Lex_Map[(*p)] > (LEX_SPECIAL|LEX_SPECIAL_COLON)) {
                if (IS_LEX_NUMBER(*p) || *p == '\'') {
                    p++;
                } else if (*p == 'e' || *p == 'E') {
                    if (has_exp) tokenError(p, err_invalid_token);
                    has_exp = true;
                    id = TokenIdFloat;
                    p++;
                    if (*p == '-' || *p == '+') p++;
                } else if (*p == '.' || *p == ',') {
                    if (has_exp || has_dot)
                        tokenError(p, err_invalid_token);
                    has_dot = true;
                    id = TokenIdFloat;
                    p++;
                } else tokenError(p, err_invalid_token);
            }
            if (has_sign) start--;
            cur = cur->next = newToken(ctx.TokBuf, id, start, p - start);
            continue;
        }

        // Word
        len = scanSymbol(p);
        if (len) {
            auto id = has_apostrophe ? TokenIdLitWord : getSymbolId(p, len);
            ADD_TOK(id, p, len);
            continue;
        } else if (has_apostrophe) {p--;}

        if (*p) tokenError(p, err_invalid_token);
    }

    *end_token = cur;
    cur = cur->next = newToken(ctx.TokBuf, TokenIdEof, p, 0);
    return head.next;
}

template <typename... Args>
Token * Tokenizer::tokenError(const char *loc, unsigned diag_id, Args &&... args) {
    ctx.report(loc, diag_id, std::forward<Args>(args)...);
    return newToken(ctx.TokBuf, TokenIdUnknown, loc, 0);
}