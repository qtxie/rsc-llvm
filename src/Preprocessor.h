/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_PREPROCESSOR_H
#define RED_SYSTEM_PREPROCESSOR_H

#include "Utils.h"
#include "_StringMap.h"

using std::unique_ptr;
using redsc::StringMap;

typedef Buffer<Hideset, 5000> HidesetBuffer;
typedef SmallVector<StringRef> MacroParam;
typedef unique_ptr<MacroParam> MacroParamPtr;

struct MacroArg {
    StringRef name;
    Token *tok;
};

struct Macro {
    StringRef name;
    bool is_simple;     // Simple macro or function-like
    uint32_t body_len;
    Token *body;
    MacroParamPtr params;
};

class Compilation;

class Preprocessor : protected Utils
{
private:
    StringMap<Macro> macros;
    HidesetBuffer hsBuf;

public:
    Preprocessor(Compilation &c) : Utils(c) {};
    Preprocessor(Compilation &&) = delete;  // prevents rvalue binding

    Token * preprocess(Token *tok);

private:
    bool expandMacro(Token *tok, Token **rest);
    Macro *findMacro(Token *tok);
    Macro &addMacro(StringRef name, Token *body, uint32_t body_len, bool is_simple);

    Token * addHideset(Token *body, uint32_t body_len, Hideset &hs);

    MacroParamPtr parseParams(Token *tok, Token **rest);
    void parseDefine(Token *tok, Token **rest);

    Token * copyToken(Token *tok);
    Hideset * newHideset() {return new (hsBuf.alloc()) Hideset;}

    template <typename... Args>
    void error(const Token *tok, unsigned diag_id, Args &&... args);
};

#endif /* RED_SYSTEM_PREPROCESSOR_H */
