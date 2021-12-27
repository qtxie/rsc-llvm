/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: The compilation context for each compilation unit
*/

#ifndef RED_SYSTEM_COMPILATION_H
#define RED_SYSTEM_COMPILATION_H

#include "LLVM.h"
#include "Diagnostic.h"
#include "Token.h"
#include <llvm/Support/FormatVariadic.h>

class Compilation {
public:
    std::string MainFile;
    std::string CurrentPath;
    std::vector<std::string> Paths;
    llvm::SourceMgr SrcMgr;
    TokenBuffer TokBuf;

    Compilation(std::string filepath) : MainFile(filepath) {}

    void run();

    std::string toNativePath(const char *p, int len);
    const char* readFile(const std::string &filepath);
    Token * tokenizeFile(const std::string &filepath, Token **end_token);

    template <typename... Args>
    void report(const char *loc, unsigned diag_id, Args &&... args) {
        std::string msg = llvm::formatv(getDiagnosticText(diag_id), std::forward<Args>(args)...).str();
        llvm::SourceMgr::DiagKind kind = getDiagnosticKind(diag_id);
        SrcMgr.PrintMessage(llvm::SMLoc::getFromPointer(loc), kind, msg);
        assert(false);
        if (kind == llvm::SourceMgr::DK_Error)
            exit(EXIT_FAILURE);
    }
};

#endif /* RED_SYSTEM_COMPILATION_H */
