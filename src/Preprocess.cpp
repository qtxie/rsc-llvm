/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

// This file implements the Red/System directives.
//
// The macro expansion algorithm is the same as the C one.
// Tt's guaranteed to stop even if there is a recursive macro.
// Informally speaking, a macro is applied only once for each token.
// That is, if a macro token T appears in a result of direct or
// indirect macro expansion of T, T won't be expanded any further.
// For example, if T is defined as U, and U is defined as T, then
// token T is expanded to U and then to T and the macro expansion
// stops at that point.
//
// To achieve the above behavior, we attach for each token a set of
// macro names from which the token is expanded. The set is called
// "hideset". Hideset is initially empty, and every time we expand a
// macro, the macro name is added to the resulting tokens' hidesets.
//
// The above macro expansion algorithm is explained in this document
// written by Dave Prossor, which is used as a basis for the
// standard's wording:
// https://www.spinellis.gr/blog/20060626/cpp.algo.pdf

#include "Preprocess.h"
#include "Compilation.h"

Token * Preprocess::copyToken(Token *tok) {
    Token *t = new (ctx.TokBuf.alloc()) Token{*tok};
    t->next = nullptr;
    return t;
}

bool Preprocess::expandMacro(Token *tok, Token **rest) {
    if (tok->hideset) {
        Hideset &hs = *tok->hideset;
        for (auto &name : hs) {
            if (name.equals_insensitive(tok->str())) {
                return false;
            }
        }
    }

    Macro *m = findMacro(tok);
    if (!m) return false;

    if (!tok->hideset) tok->hideset = newHideset();
    if (m->is_simple) {
        auto& hs = *tok->hideset;
        hs.push_back(tok->str());
        Token *body = addHideset(m->body, m->body_len, hs);
        (*body)[m->body_len - 1].next = tok->next;
        if (m->body_len == 1) {
            *tok = *body;   // expand the macro in-place
            *rest = tok;
        } else {
            *rest = body;
        }
        return true;
    }

    // Parametrized macro

    return false;
}

Token * Preprocess::addHideset(Token *body, uint32_t body_len, Hideset &hs) {
    Token head = {};
    Token *cur = &head;

    while (body_len--) {
        Token *t = copyToken(body);
        t->hideset = newHideset();
        t->hideset->append(hs);
        cur = cur->next = t;
        body = body->next;
    }
    return head.next;
}

Macro *Preprocess::findMacro(Token *tok) {
    if (tok->id != TokenIdWord) return nullptr;
    if (auto it = macros.find(tok->str()); it != macros.end()){
        return &it->second;
    }
    return nullptr;
}

Macro &Preprocess::addMacro(StringRef name, Token *body, uint32_t body_len, bool is_simple) {
    auto&& [keyval, is_new] = macros.try_emplace(name, 
        Macro{.name = name, .is_simple = is_simple, .body_len = body_len, .body = body});
    if (!is_new)
        ctx.report(name.data(), warn_macro_redefined);
    return keyval->second;
}

Token * Preprocess::skip(Token *tok, TokenId id, const char name[]) {
    if (tok->id != id)
        error(tok, err_expected, name);
    return tok->next;
}

// Returns the number of tokens inside the []
uint32_t Preprocess::skipContainer(Token *tok, Token **rest, TokenId beg, TokenId end) {
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
        error(start, err_expected_match, ']', '[');
    *rest = tok;
    return n;
}

MacroParamPtr Preprocess::parseParams(Token *tok, Token **rest) {
    auto params = std::make_unique<MacroParam>();

    while (tok->id != TokenIdRParen)
    {
        if (tok->id != TokenIdWord)
            error(tok, err_expected, "word");

        params->push_back(tok->str());
        tok = tok->next;
    }

    *rest = tok;
    return params;
}

void Preprocess::parseDefine(Token *tok, Token **rest) {
    if (tok->id != TokenIdWord)
        error(tok, err_expected, "word");
    StringRef name(tok->loc, tok->len);
    tok = tok->next;

    if (tok->id == TokenIdLParen) {
        // Parametrized macro
        auto params = parseParams(tok->next, &tok);
        uint32_t n = 0;
        if (tok->id == TokenIdLBracket) {
            n = skipBlock(tok, rest);
            n -= 2;  // don't count the outest []
            tok = tok->next;
        } else if (tok->id == TokenIdLParen) {
            n = skipParen(tok, rest);
        } else {
            error(tok, err_expected, "block! or paren! value");
        }
        auto& m = addMacro(name, tok, n, false);
        m.params = std::move(params);
    } else {
        // Simple macro
        uint32_t n = 0;
        if (tok->id == TokenIdLBracket) {
            n = skipBlock(tok, rest);
            n -= 2;  // don't count the outest []
            tok = tok->next;
        } else {
            n = 1;
            *rest = tok->next;
        }
        addMacro(name, tok, n, true);
    }
}

Token * Preprocess::preprocess(Token *tok) {
    Token head = {};
    Token *cur = &head;

    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdWord && expandMacro(tok, &tok))
            continue;

        if (tok->kind != TokenIdDirective) {
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        if (tok->id == TokenIdMacroInclude) {
            Token *file = tok->next;
            if (file->id != TokenIdFile)
                error(file, err_expected, "filename");

            Token *tk_end = nullptr;
            tok = ctx.tokenizeFile(ctx.toNativePath(file->loc, file->len), &tk_end, true);
            tk_end->next = file->next;
            continue;
        }

        if (tok->id == TokenIdMacroDefine) {
            parseDefine(tok->next, &tok);
            continue;
        } else {
            // TODO: process other directives
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }
    }

    return head.next;
}

template <typename... Args>
void Preprocess::error(const Token *tok, unsigned diag_id, Args &&... args) {
    ctx.report(tok->loc, diag_id, std::forward<Args>(args)...);
}