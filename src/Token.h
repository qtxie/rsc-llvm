/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_TOKEN_H
#define RED_SYSTEM_TOKEN_H

#include "Buffer.h"
#include <llvm/ADT/StringRef.h>


enum TokenKind : uint8_t {
    TokenKindAny,
    TokenKindWord
};

enum TokenId : uint8_t {
    #define TOK(ID) TokenId ## ID,
    #include "TokenId.def"
    TokenIdCount
};

struct Hideset {
    Hideset *next;
    char *name;
};

struct Token {
    TokenId id;         // Token ID
    TokenKind kind;     // Token Kind
    int len;            // Token length
    const char *loc;    // Token location
    Token *next;        // Next Token
    Hideset *hideset;   // For macro expansion

    // Helper functions
    StringRef name() {return StringRef(loc, len);}
};

typedef uint32_t TokenIndex;
typedef Buffer<Token> TokenBuffer; 

const char * const TokenNames[] = {
    #define TOK(ID) #ID,
    #define KEYWORD(ID, ID_LOWER) #ID,
    #include "TokenId.def"
    nullptr
};

#endif /* RED_SYSTEM_TOKEN_H */
