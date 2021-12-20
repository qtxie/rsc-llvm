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

    void tokenize(const char *buffer);

private:
    uint32_t decodeUtf8(const char *p, const char **new_pos);

    int scanSymbol(const char *p);

    const char * scanBinary(const char *p);

    const char * scanByte(const char *p);

    const char * scanDirectives(const char *p);

    bool isHexChar(int c) {return strchr("ABCDEF", c);}

    bool isDelimiter(int c) {
        return (strchr(" \t\n\r[]();{}\"\v\f", c) || c == 0);
    }

    Token * newToken(TokenBuffer &buf, TokenId id, const char *start, int len) {
        return new (buf.alloc()) Token{id, len, start};
    }
};

#endif /* RED_SYSTEM_TOKENIZER_H */
