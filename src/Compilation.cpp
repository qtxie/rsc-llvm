/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: The compilation context for each compilation unit
*/

#include <filesystem>
#include "Compilation.h"
#include "Tokenizer.h"
#include "Preprocessor.h"
#include "Parser.h"
#include "CodeGen.h"
#include "TimeMeasuring.h"
#include <llvm/Support/WithColor.h>
#include <llvm/Support/Path.h>
#include <lld/Common/Driver.h>
#include <lld/Common/ErrorHandler.h>

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

void Compilation::linkElf(llvm::SmallSet<StringRef, 4> &import_libs) {
    std::vector<const char *> argv{"-error-limit=0"};

    argv.push_back("-z");
    argv.push_back("stack-size=16777216");

    argv.push_back("--gc-sections");

    argv.push_back("--entry");
    argv.push_back(entryFuncName.c_str());

    argv.push_back("-m");
    argv.push_back("elf_x86_64");

    //argv.push_back("-static");
    argv.push_back("-pie");

    auto out_file = outputFilepath;
    argv.push_back("-o");
    argv.push_back(out_file.c_str());

    auto obj_file = outputFilepath;
    argv.push_back(obj_file.append(OBJ_EXT).c_str());

    argv.push_back("-L");
    argv.push_back("/lib/x86_64-linux-gnu");
    argv.push_back("-lc");

    if (!lld::elf::link(argv, false, llvm::outs(), llvm::errs()))
        lld::exitLld(-1);
}

std::u8string Compilation::findLib(std::string name) {
    namespace fs = std::filesystem;
    for (const auto &path : libsDir) {
        auto libpath = fs::path(path) / name;
        if (fs::exists(libpath)) return libpath.u8string();
    }
    return std::u8string();
}

void Compilation::linkCoff(llvm::SmallSet<StringRef, 4> &import_libs) {
    std::vector<const char *> argv{"-ERRORLIMIT:0", "-NOLOGO"};
    argv.push_back("-DEBUG");
    argv.push_back("-STACK:16777216");
    argv.push_back("-MACHINE:X64");

    auto out_file = outputFilepath + ".exe";
    argv.push_back(out_file.insert(0, "-OUT:").c_str());

    auto obj_file = outputFilepath;
    argv.push_back(obj_file.append(OBJ_EXT).c_str());

    argv.push_back("-SUBSYSTEM:console");
    argv.push_back("-NODEFAULTLIB");
    auto entry = std::string("-ENTRY:") + entryFuncName;
    argv.push_back(entry.c_str());

    std::vector<std::u8string> libs;
    for (auto &lib : import_libs) {
        SmallVector<char> libname(lib.begin(), lib.end());
        llvm::sys::path::replace_extension(libname, ".lib");
        auto libpath = findLib(std::string(libname.data(), libname.size()));
        if (!libpath.empty()) {
            libs.push_back(libpath);
            argv.push_back((char *)libs.back().c_str());
        }
    }
    if (!lld::coff::link(argv, false, llvm::outs(), llvm::errs()))
        lld::exitLld(-1);
}

void Compilation::link(llvm::SmallSet<StringRef, 4> &import_libs) {
#ifdef _WIN32
    linkCoff(import_libs);
#else
    linkElf(import_libs);
#endif
    llvm::outs() << "Output file: " << outputFilepath + BIN_EXT << '\n';
}

void Compilation::run() {
    TimeMeasuring t(true);

    t.insertBreakpoint("tokenizer");
    Token *end_tok = nullptr;
    Token *tok = tokenizeFile(mainFile, &end_tok);
    llvm::outs() << "Tokenize time: " << t.getTimeFromBreakpoint("tokenizer") << "ms\n";

    dumpTokens(tok, "All the tokens:");

    tok = parseHeader(tok);

    t.insertBreakpoint("preprocess");
    auto pp = Preprocessor(*this);
    tok = pp.preprocess(tok);
    llvm::outs() << "Preprocess time: " << t.getTimeFromBreakpoint("preprocess") << "ms\n";

    dumpTokens(tok, "Preprocessed tokens:");

    auto ext = llvm::sys::path::extension(mainFile);
    outputFilepath = std::string(mainFile).erase(mainFile.length() - ext.size());

    auto codegen = CodeGen(*this);
    t.insertBreakpoint("parse");
    auto parser = Parser(*this, codegen);
    auto prog = parser.parse(tok);
    llvm::outs() << "Parse time: " << t.getTimeFromBreakpoint("parse") << "ms\n";

    t.insertBreakpoint("codegen");
    codegen.setParser(&parser);
    codegen.emit(prog);
    llvm::outs() << "CodeGen time: " << t.getTimeFromBreakpoint("codegen") << "ms\n";

    t.insertBreakpoint("link");
    if (!emitIr) link(codegen.import_libs);
    llvm::outs() << "Link time: " << t.getTimeFromBreakpoint("link") << "ms\n";
}