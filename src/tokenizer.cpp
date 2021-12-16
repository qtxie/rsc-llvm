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

struct TState {
    Token tok;
    TokenList *tokens;
    bool has_space = false;  // True if the token follows a space char
    std::string *filepath;
};

static bool isHexChar(int c) {
    return strchr("ABCDEF", c);
}

static void addToken(TState &s, TokenId id, const char *start, int len) {

}

static void updateLastToken(TState &s, TokenId id, const char *end) {

}

inline static bool isEndChars(int c) {
    return (isspace(c) || strchr("[]();{\"", c) || c == 0);
}

void errorToken(TState &s, char *fmt, ...) {
    llvm::errs() << "error token: " << "\n";
    exit(1);
}

void tokenize(const char *p, Compilation &ctx) {
    // skip the UTF-8 "BOM" marker
    if (!memcmp(p, "\xef\xbb\xbf", 3)) {
        p += 3;
    }

    TState state{};
    state.tokens = &ctx.Tokens;

    while (*p) {
        // skip line comments
        if (*p == ';') {
            while (*p != '\n') p++;
            state.has_space = true;
            continue;
        }

        // skip whitespace characters
        // ' ','\t','\n','\r','\v','\f'
        if (isspace(*p)) {
            p++;
            state.has_space = true;
            continue;
        }

        // Numeric literal
        // e.g. 123  1e3  1.2  .12  ,12  123'45  123'45E+2  1,23e-2
        if (isdigit(*p) || ((*p == '.' || *p == ',') && isdigit(p[1]))) {
            TokenId id = isdigit(*p) ? TokenIdIntLiteral : TokenIdFloatLiteral;
            bool has_dot = (id == TokenIdFloatLiteral);
            bool has_exp = false;
            const char *q = p++;
            while (!isEndChars(*p)) {
                if (isdigit(*p) || *p == '\'') {
                    p++;
                } else if (*p == 'e' || *p == 'E') {
                    if (has_exp) errorToken(state, "Invalid float number");
                    has_exp = true;
                    id = TokenIdFloatLiteral;
                    p++;
                    if (*p == '-' || *p == '+') p++;
                } else if (*p == '.' || *p == ',') {
                    if (has_exp || has_dot)
                        errorToken(state, "Invalid float number");
                    has_dot = true;
                    id = TokenIdFloatLiteral;
                    p++;
                } else errorToken(state, "Invaild number");
            }
            addToken(state, id, q, p - q);
            continue;
        }

        switch (*p)
        {
        #define CASE(ch, tok)                              \
            case ch:                                       \
                addToken(state, tok, p, 1);                \
                p++;                                       \
                break

        CASE('+', TokenIdPlus);
        CASE('-', TokenIdMinus);
        CASE('*', TokenIdStar);
        CASE('/', TokenIdSlash);
        CASE('[', TokenIdLBracket);
        CASE(']', TokenIdRBracket);
        CASE('=', TokenIdEq);
        CASE('#', TokenIdHash);
        CASE(')', TokenIdRBrace);

        default:
            break;
        }
    }
}
