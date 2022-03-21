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
#include <llvm/Target/TargetMachine.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/ADT/SmallSet.h>

#ifdef _WIN32
    #define OBJ_EXT ".obj"
    #define BIN_EXT ".exe"
#else
    #define OBJ_EXT ".o"
    #define BIN_EXT ""
#endif

using llvm::SMLoc;

using OptimizationLevel = typename llvm::PassBuilder::OptimizationLevel;

class Compilation {
public:
    std::string mainFile;
    std::string appDataDir;
    std::vector<std::string> &libsDir;
    llvm::TargetMachine *TM;
    OptimizationLevel optLevel;
    bool emitIr;
    std::string currentPath;
    std::vector<std::string> paths;
    llvm::SourceMgr srcMgr;
    TokenBuffer tokBuf;

    // Configuration
    void *baseAddress;
    int arch;
    int abi;
    std::string os;
    std::string subSystem;
    std::vector<StringRef> modules;
    std::string outputFilepath;
    std::string entryFuncName;

    Compilation(
        std::string filepath,
        std::string data_dir,
        std::vector<std::string> &libs_dir,
        llvm::TargetMachine *target,
        OptimizationLevel level,
        bool emit_llvm = false
        ) : mainFile(filepath), appDataDir(data_dir), libsDir(libs_dir), TM(target), optLevel(level), emitIr(emit_llvm) {
        entryFuncName = "__Red/System__MainStartup";
    }

    void run();
    void linkElf(llvm::SmallSet<StringRef, 4> &import_libs);
    void linkCoff(llvm::SmallSet<StringRef, 4> &import_libs);
    void link(llvm::SmallSet<StringRef, 4> &import_libs);

    std::u8string findLib(std::string name);

    std::string toNativePath(const char *p, int len);
    const char* readFile(const std::string &filepath, SMLoc include_loc);
    Token * tokenizeFile(const std::string &filepath, 
                         Token **end_token, 
                         bool skip_header = false,
                         SMLoc include_loc = SMLoc());

    template <typename... Args>
    void error(const char *loc, unsigned diag_id, Args &&... args) {
        std::string msg = llvm::formatv(getDiagnosticText(diag_id), std::forward<Args>(args)...).str();
        llvm::SourceMgr::DiagKind kind = getDiagnosticKind(diag_id);
        srcMgr.PrintMessage(llvm::SMLoc::getFromPointer(loc), kind, msg);
        if (kind == llvm::SourceMgr::DK_Error) {
            assert(false);
            exit(EXIT_FAILURE);
        }
    }

    template <typename... Args>
    void error(const Token *tok, unsigned diag_id, Args &&... args) {
        error(tok->loc, diag_id, std::forward<Args>(args)...);
    }

    // Utility functions
    Token * checkHeader(Token *tok, bool skip_header);
    Token * parseHeader(Token *tok);
};

#endif /* RED_SYSTEM_COMPILATION_H */
