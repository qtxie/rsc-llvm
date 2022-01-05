#include "Utils.h"
#include "Compilation.h"

Token * Utils::skip(Token *tok, TokenId id, char delimiter) {
    if (tok->id != id)
        comp.error(tok, err_expected, delimiter);
    return tok->next;
}

void Utils::expect(Token *tok, TokenId id, char delimiter) {
    if (tok->id != id)
        comp.error(tok, err_expected, delimiter);
}

bool Utils::consume(Token *tok, TokenId id, Token **rest) {
    if (tok->id == id) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// `rest` points to the tok next to the end tok if success.
uint32_t Utils::skipContainer(Token *tok, Token **rest, TokenId beg, TokenId end) {
    Token *start = tok;
    uint32_t level = 0;
    uint32_t n = 0;
    do {
        if (tok->id == beg)
            level++;
        else if (tok->id == end)
            level--;
        n++;
        tok = tok->next;   
    } while (level && tok->id != TokenIdEof);
    if (level)
        comp.error(start, err_expected_match, ']', '[');
    *rest = tok;
    return n;
}