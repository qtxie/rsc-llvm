/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "tokenizer.h"
#include "compilation.h"
#include <cctype>
#include <llvm/Support/raw_ostream.h>

static bool isHexChar(int c) {
    return strchr("ABCDEF", c);
}

static void addToken(TokenList &tokens, TokenId id, const char *start, int len) {
    tokens.push_back(Token{id, start, len});
}

inline static bool isEndChars(int c) {
    return (isspace(c) || strchr("[]();{\"", c) || c == 0);
}

const char* scanBinary(Compilation &ctx, const char *p) {
    return p;
}

const char* scanByte(Compilation &ctx, const char *p) {
    return p;
}

const char* scanDirectives(Compilation &ctx, const char *p) {
    return p;
}

void reportError(Compilation &ctx, char *fmt, ...) {
    llvm::errs() << "error token: " << "\n";
    exit(1);
}

void tokenize(Compilation &ctx, const char *p) {
    // Skip the UTF-8 "BOM" marker
    if (!memcmp(p, "\xef\xbb\xbf", 3)) {
        p += 3;
    }

    while (*p) {
        // Skip line comments
        if (*p == ';') {
            // NOTE: we don't bother with \r as the line break as modern OSes don't use it.
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
            const char *q = p++;
            if (*p == '{')           // Binary arrays
                p = scanBinary(ctx, p);
            else if (*p == '"')
                p = scanByte(ctx, p);
            else
                p = scanDirectives(ctx, p);
            continue;
        }


        // Delimiters
        switch (*p)
        {
        #define CASE(ch, tok_id)                           \
            case ch:                                       \
                addToken(ctx.Tokens, tok_id, p, 1);        \
                p++;                                       \
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
            while (!isEndChars(*p)) {
                if (isdigit(*p) || *p == '\'') {
                    p++;
                } else if (*p == 'e' || *p == 'E') {
                    if (has_exp) ctx.report(p, err_invalid_token);
                    has_exp = true;
                    id = TokenIdFloatLiteral;
                    p++;
                    if (*p == '-' || *p == '+') p++;
                } else if (*p == '.' || *p == ',') {
                    if (has_exp || has_dot)
                        ctx.report(p, err_invalid_token);
                    has_dot = true;
                    id = TokenIdFloatLiteral;
                    p++;
                } else ctx.report(p, err_invalid_token);
            }
            addToken(ctx.Tokens, id, q, p - q);
            continue;
        }

        // Any words
        ctx.report(p, err_invalid_token);
    }
}
