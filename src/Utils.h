#ifndef RED_SYSTEM_UTILS_H
#define RED_SYSTEM_UTILS_H

#include "Token.h"

class Compilation;

class Utils
{
public:
    Compilation &comp;

    Utils(Compilation &c) : comp(c) {};
    Utils(Compilation &&) = delete;  // prevents rvalue binding

    Token * skip(Token *tok, TokenId id, char delimiter);
    bool consume(Token *tok, TokenId id, Token **rest);

    uint32_t skipContainer(Token *tok, Token **rest, TokenId beg, TokenId end);

    // Returns the number of tokens skipped, include []
    uint32_t skipBlock(Token *tok, Token **rest) {
        return skipContainer(tok, rest, TokenIdLBracket, TokenIdRBracket);
    }

    // Returns the number of tokens skipped, include ()
    uint32_t skipParen(Token *tok, Token **rest) {
        return skipContainer(tok, rest, TokenIdLParen, TokenIdRParen);
    }
};



#endif /* RED_SYSTEM_UTILS_H */
