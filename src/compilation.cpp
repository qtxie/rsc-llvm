/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: The compilation context for each file
*/

#include "compilation.h"
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/WithColor.h>

void Compilation::run() {
    const char *buffer = readFile(MainFile);
    tokenize(buffer, *this);
}

const char* Compilation::readFile(std::string &filepath) {   
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_or_err = llvm::MemoryBuffer::getFile(filepath);

    if (std::error_code err = file_or_err.getError()) {
      llvm::WithColor::error(llvm::errs()) << "Error reading " << filepath << ": " << err.message() << "\n";
    }

    auto id = SrcMgr.AddNewSourceBuffer(std::move(*file_or_err), llvm::SMLoc());
    File f;
    Files.push_back(f);
    return SrcMgr.getMemoryBuffer(id)->getBuffer().begin();
}


Compilation::~Compilation() {}