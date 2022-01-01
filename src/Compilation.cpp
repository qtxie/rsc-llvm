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
        return CurrentPath + s;
}

const char* Compilation::readFile(const std::string &filepath, llvm::SMLoc include_loc) {
    //llvm::errs() << "Reading file: " << filepath << "\n";
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_or_err = llvm::MemoryBuffer::getFile(filepath);

    if (std::error_code err = file_or_err.getError()) {
        llvm::WithColor::error(llvm::errs()) << "Error reading " << filepath << ": " << err.message() << "\n";
        exit(EXIT_FAILURE);
    }

    auto id = SrcMgr.AddNewSourceBuffer(std::move(*file_or_err), include_loc);
    return SrcMgr.getMemoryBuffer(id)->getBuffer().begin();
}

Token * Compilation::tokenizeFile(const std::string &filepath, Token **end_token, bool skip_header, SMLoc include_loc) {
    auto parent_path = llvm::sys::path::parent_path(filepath).str();
    if (parent_path != llvm::sys::path::root_path(filepath))  // if parent_path is not X:\ 
        parent_path.push_back(OS_DIR_SEP);

    Paths.push_back(CurrentPath);
    CurrentPath = parent_path;

    const char *buffer = readFile(filepath, include_loc);
    Token *tok = Tokenizer(*this).tokenize(buffer, end_token);
    Token &t = *tok;

    // Check header
    if (t.equals("Red") && t[1].equals("/") && t[2].equals("System") && t[3].id == TokenIdLBracket) {
        tok = t + 3;

        // Skip header block
        if (skip_header) {
            uint32_t level = 0;
            do {
                if (tok->id == TokenIdLBracket)
                    level++;
                else if (tok->id == TokenIdRBracket)
                    level--;
                tok = tok->next;
            } while (level && tok->id != TokenIdEof);

            if (level)
                report(t[3].loc, err_expected_match, ']', '[');
        }
    } else {
        report(tok->loc, err_wrong_header);
    }

    CurrentPath = Paths.back();
    Paths.pop_back();

    return tok;
}

void Compilation::run() {
    TimeMeasuring t(true);

    t.insertBreakpoint("tokenizer");
    Token *end_tok = nullptr;
    Token *tok = tokenizeFile(MainFile, &end_tok);
    llvm::errs() << "Tokenize time: " << t.getTimeFromBreakpoint("tokenizer") << "ms\n";

    llvm::errs() << "All the tokens: \n";
    Token *tk = tok;
    while (tk) {
        llvm::errs() << TokenNames[tk->id] << ":" << tk->str() << " ";
        tk = tk->next;
    }
    t.insertBreakpoint("preprocess");
    tok = Preprocessor(*this).preprocess(tok);
    llvm::errs() << "\nPreprocess time: " << t.getTimeFromBreakpoint("preprocess") << "ms\n";

    llvm::errs() << "Preprocessed tokens: \n";
    tk = tok;
    while (tk) {
        llvm::errs() << TokenNames[tk->id] << ":" << tk->str() << " ";
        tk = tk->next;
    }

    t.insertBreakpoint("parse");
    auto prog = Parser(*this).parse(tok);
    llvm::errs() << "\nPreprocess time: " << t.getTimeFromBreakpoint("parse") << "ms\n";

}