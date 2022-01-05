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
    Compilation &comp;

public:
    Tokenizer(Compilation &c) : comp(c) {};
    Tokenizer(Compilation &&) = delete;  // prevents rvalue binding

    Token * tokenize(const char *buffer, Token **end_token);

private:
    uint32_t decodeUtf8(const char *p, const char **new_pos);

    int scanSymbol(const char *p);
    int scanFile(const char *p);
    int scanString(const char *p);
    int scanMultilineString(const char *p);
    int scanBinary(const char *p);
    int scanByte(const char *p);

    Token * scanDirectives(const char *p);

    Token * newToken(TokenBuffer &buf, TokenId id, const char *start, int len) {
        Token *t = buf.alloc();
        t->id = id;
        t->sub_id = id;
        t->len = len;
        t->loc = start;
        return t;
    }

    template <typename... Args>
    Token * tokenError(const char *loc, unsigned diag_id, Args &&... args);
};

#endif /* RED_SYSTEM_TOKENIZER_H */
