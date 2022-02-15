/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_TOKEN_H
#define RED_SYSTEM_TOKEN_H

#include "LLVM.h"
#include "Buffer.h"
#include <llvm/ADT/StringRef.h>

enum TokenId : uint8_t {
    #define TOK(ID) TokenId ## ID,
    #include "TokenId.def"
    TokenIdCount
};

#define TkFlagEscapedString  1

typedef SmallVector<StringRef> Hideset;

struct Token {
    TokenId id;         // Token ID
    TokenId sub_id;     // e.g Multiline String
    uint8_t flags;
    int len;            // Token length
    const char *loc;    // Token location
    Token *next;        // Next Token
    Hideset *hideset;   // For macro expansion

    // Helper functions
    constexpr StringRef str() {return StringRef(loc, len);}

    // return true if failure
    bool toInteger(int64_t &result) {
        return str().getAsInteger(id == TokenIdHex ? 16 : 10, result);
    }

    // return true if failure
    bool toDouble(double &result) {
        return str().getAsDouble(result);
    }

    // case insensitive comparision
    bool equals(const StringLiteral s) const {
        return StringRef(loc, len).equals_insensitive(s);
    }
    bool equals(const StringRef s) {
        return StringRef(loc, len).equals_insensitive(s);
    }
    bool equals(Token *tok) const {
        return StringRef(loc, len).equals_insensitive(tok->str());
    }

    // case sensitive comparision
    bool operator==(const StringLiteral s) const {
        return StringRef(loc, len).equals(s);
    }

    Token * operator+(std::size_t n) {
        Token *tok = this;
        while (n--) tok = tok->next;
        return tok;
    }

    Token& operator[](std::size_t idx) {
        Token *tok = this;
        while (idx--) tok = tok->next;
        return *tok;
    }
};

typedef uint32_t TokenIndex;
typedef Buffer<Token> TokenBuffer; 

const char * const TokenNames[] = {
    #define TOK(ID) #ID,
    #define KEYWORD(ID, ID_LOWER) #ID,
    #include "TokenId.def"
    nullptr
};

void dumpTokens(Token *tok, const char *msg);

#endif /* RED_SYSTEM_TOKEN_H */
