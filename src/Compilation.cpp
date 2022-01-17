/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: The compilation context for each compilation unit
*/

#include "Compilation.h"
#include "Tokenizer.h"
#include "Preprocessor.h"
#include "Parser.h"
#include "CodeGen.h"
#include "TimeMeasuring.h"
#include <llvm/Support/WithColor.h>
#include <llvm/Support/Path.h>

#ifdef _WIN32
    #define OS_DIR_SEP '\\'
#else
    #define OS_DIR_SEP '/'
#endif

static bool is_absolute_path(const char *p) {
    return *p == '/';
}

// Convert R/S file path to OS native path.
static std::string to_os_path(const char *p, int len) {
    std::string s;
    char c = 0;
    const char *end = p + len;
    if (*p == '/') {
        p++;
    #ifdef _WIN32
        if (p < end) c = *p++;
        if (c != '/' && p < end && *p == '/') {  // case: %/c/
            s.push_back(c);
            s.push_back(':');
            p++;
        } else {
            s.push_back(OS_DIR_SEP);
            p--;
        }
    #endif
        s.push_back(OS_DIR_SEP);
    }

    while (p < end) {
        c = (*p == '/') ? OS_DIR_SEP : *p;
        s.push_back(c);
        p++;
    }
    return s;
}

std::string Compilation::toNativePath(const char *p, int len) {
    bool is_absolute = is_absolute_path(p);
    auto s = to_os_path(p, len);
    if (is_absolute)
        return s;
    else
        return currentPath + s;
}

const char* Compilation::readFile(const std::string &filepath, llvm::SMLoc include_loc) {
    //llvm::errs() << "Reading file: " << filepath << "\n";
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_or_err = llvm::MemoryBuffer::getFile(filepath);

    if (std::error_code err = file_or_err.getError()) {
        llvm::WithColor::error(llvm::errs()) << "Error reading " << filepath << ": " << err.message() << "\n";
        exit(EXIT_FAILURE);
    }

    auto id = srcMgr.AddNewSourceBuffer(std::move(*file_or_err), include_loc);
    return srcMgr.getMemoryBuffer(id)->getBuffer().begin();
}

Token * Compilation::tokenizeFile(const std::string &filepath, Token **end_token, bool skip_header, SMLoc include_loc) {
    auto parent_path = llvm::sys::path::parent_path(filepath).str();
    if (parent_path != llvm::sys::path::root_path(filepath))  // if parent_path is not X:\ 
        parent_path.push_back(OS_DIR_SEP);

    paths.push_back(currentPath);
    currentPath = parent_path;

    const char *buffer = readFile(filepath, include_loc);
    Token *tok = Tokenizer(*this).tokenize(buffer, end_token);

    tok = checkHeader(tok, skip_header);

    currentPath = paths.back();
    paths.pop_back();

    return tok;
}

Token * Compilation::checkHeader(Token *tok, bool skip_header) {
    Token &t = *tok;
    if (t == "Red" && t[1] == "/" && t[2] == "System" && t[3].id == TokenIdLBracket) {
        // Skip header block
        if (skip_header) {
            tok = t + 3;
            uint32_t level = 0;
            do {
                if (tok->id == TokenIdLBracket)
                    level++;
                else if (tok->id == TokenIdRBracket)
                    level--;
                tok = tok->next;
            } while (level && tok->id != TokenIdEof);

            if (level)
                error(t[3].loc, err_expected_match, ']', '[');
        }
    } else {
        error(tok->loc, err_wrong_header);
    }
    return tok;
}

Token * Compilation::parseHeader(Token *tok) {
    tok = checkHeader(tok, false);
    for (int i = 3; i; i--) {
        tok = tok->next;
    }
    assert(tok->id == TokenIdLBracket);

    uint32_t level = 0;
    do {
        if (tok->id == TokenIdLBracket)
            level++;
        else if (tok->id == TokenIdRBracket)
            level--;
        else if (tok->id == TokenIdSetWord) {
            // TODO key
        } else {
            // TODO value
        }
        tok = tok->next;
    } while (level && tok->id != TokenIdEof);
    return tok;
}

void Compilation::run() {
    TimeMeasuring t(true);

    t.insertBreakpoint("tokenizer");
    Token *end_tok = nullptr;
    Token *tok = tokenizeFile(mainFile, &end_tok);
    llvm::errs() << "Tokenize time: " << t.getTimeFromBreakpoint("tokenizer") << "ms\n";

dumpTokens(tok, "All the tokens:");

    tok = parseHeader(tok);

    t.insertBreakpoint("preprocess");
    auto pp = Preprocessor(*this);
    tok = pp.preprocess(tok);
    llvm::errs() << "Preprocess time: " << t.getTimeFromBreakpoint("preprocess") << "ms\n";

dumpTokens(tok, "Preprocessed tokens:");

    auto codegen = CodeGen(*this);
    t.insertBreakpoint("parse");
    auto parser = Parser(*this, codegen);
    auto prog = parser.parse(tok);
    llvm::errs() << "Parse time: " << t.getTimeFromBreakpoint("parse") << "ms\n";

}