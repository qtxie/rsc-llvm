/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_TOKENIZER_H
#define RED_SYSTEM_TOKENIZER_H

#include <cctype>
#include "LLVM.h"
#include "Token.h"

class Compilation;

class Tokenizer
{
private:
    Compilation &ctx;

public:
    Tokenizer(Compilation &c) : ctx(c) {};
    Tokenizer(Compilation &&) = delete;  // prevents rvalue binding

    Token * tokenize(const char *buffer, Token **end_token);

private:
    uint32_t decodeUtf8(const char *p, const char **new_pos);

    int scanSymbol(const char *p);
    int scanFile(const char *p);
    int scanString(const char *p);

    const char * scanBinary(const char *p);
    const char * scanByte(const char *p);

    Token * scanDirectives(const char *p);

    bool isHexChar(int c) {return strchr("ABCDEF", c);}

    bool isDelimiter(int c) {
        return (strchr(" \t\n\r[]();{}\"\v\f", c) || c == 0);
    }

    Token * newToken(TokenBuffer &buf, TokenId id, const char *start, int len) {
        return new (buf.alloc()) Token{id, TokenKindAny, len, start};
    }

    template <typename... Args>
    Token * tokenError(const char *loc, unsigned diag_id, Args &&... args) {
        ctx.report(loc, diag_id, std::forward<Args>(args)...);
        return newToken(ctx.TokBuf, TokenIdUnknown, loc, 0);
    }
};

#endif /* RED_SYSTEM_TOKENIZER_H */
