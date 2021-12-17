/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: The compilation context for each file
*/

#ifndef RED_SYSTEM_COMPILATION_H
#define RED_SYSTEM_COMPILATION_H

#include "llvm.h"
#include "tokenizer.h"
#include "diagnostic.h"
#include <string>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/SourceMgr.h>

class Compilation {
public:
    struct File {
        std::string pathname;
        unsigned int pos;       // position of the first token in `Tokens`
    };
    std::vector<File> Files;
    TokenList Tokens;
    std::string MainFile;
    llvm::SourceMgr SrcMgr;

    Compilation(std::string filepath) : MainFile(filepath) {}

    void run();

    const char* readFile(std::string &filepath);

    template <typename... Args>
    void report(const char *loc, unsigned diag_id, Args &&... args) {
        std::string msg = llvm::formatv(getDiagnosticText(diag_id), std::forward<Args>(args)...).str();
        llvm::SourceMgr::DiagKind kind = getDiagnosticKind(diag_id);
        SrcMgr.PrintMessage(llvm::SMLoc::getFromPointer(loc), kind, msg);
        if (kind == llvm::SourceMgr::DK_Error)
            exit(EXIT_FAILURE);
    }
};

#endif /* RED_SYSTEM_COMPILATION_H */
