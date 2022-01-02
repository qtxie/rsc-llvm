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

using llvm::SMLoc;

enum BuildMode {
    BuildModeDebug,
    BuildModeRelease,    // prefer fast code than small code
    BuildModeSmallRelease
};

class Compilation {
public:
    std::string mainFile;
    std::string currentPath;
    std::vector<std::string> paths;
    llvm::SourceMgr srcMgr;
    TokenBuffer tokBuf;

    // Configuration
    BuildMode buildMode;
    void *baseAddress;
    int arch;
    int abi;
    std::string os;
    std::string subSystem;
    std::vector<StringRef> modules;

    Compilation(std::string filepath) : mainFile(filepath) {}

    void run();

    std::string toNativePath(const char *p, int len);
    const char* readFile(const std::string &filepath, SMLoc include_loc);
    Token * tokenizeFile(const std::string &filepath, 
                         Token **end_token, 
                         bool skip_header = false,
                         SMLoc include_loc = SMLoc());

    template <typename... Args>
    void report(const char *loc, unsigned diag_id, Args &&... args) {
        std::string msg = llvm::formatv(getDiagnosticText(diag_id), std::forward<Args>(args)...).str();
        llvm::SourceMgr::DiagKind kind = getDiagnosticKind(diag_id);
        srcMgr.PrintMessage(llvm::SMLoc::getFromPointer(loc), kind, msg);
        if (kind == llvm::SourceMgr::DK_Error)
            assert(false);
            //exit(EXIT_FAILURE);
    }
private:
    Token * checkHeader(Token *tok, bool skip_header);
    Token * parseHeader(Token *tok);
};

#endif /* RED_SYSTEM_COMPILATION_H */
