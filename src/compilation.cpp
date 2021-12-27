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

const char* Compilation::readFile(const std::string &filepath) {
    llvm::errs() << "Reading file: " << filepath << "\n";
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_or_err = llvm::MemoryBuffer::getFile(filepath);

    if (std::error_code err = file_or_err.getError()) {
        llvm::WithColor::error(llvm::errs()) << "Error reading " << filepath << ": " << err.message() << "\n";
        exit(EXIT_FAILURE);
    }

    auto id = SrcMgr.AddNewSourceBuffer(std::move(*file_or_err), llvm::SMLoc());
    return SrcMgr.getMemoryBuffer(id)->getBuffer().begin();
}

Token * Compilation::tokenizeFile(const std::string &filepath, Token **end_token) {
    auto parent_path = llvm::sys::path::parent_path(filepath).str();
    if (parent_path != llvm::sys::path::root_path(filepath))  // if parent_path is not X:\ 
        parent_path.push_back(OS_DIR_SEP);

    Paths.push_back(CurrentPath);
    CurrentPath = parent_path;

    const char *buffer = readFile(filepath);
    Token *tok = Tokenizer(*this).tokenize(buffer, end_token);

    CurrentPath = Paths.back();
    Paths.pop_back();

    return tok;
}

void Compilation::run() {
    TimeMeasuring t(true);

    Token *end_tok = nullptr;
    Token *tok = tokenizeFile(MainFile, &end_tok);
    llvm::errs() << "Tokenize time: " << t.getTimeFromBeginning() << "ms\n";
//     while (tok) {
//         llvm::errs() << TokenNames[tok->id] << ":" << tok->name() << " ";
//        tok = tok->next;
//    }
}
