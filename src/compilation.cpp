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

void Compilation::run() {
    const char *buffer = readFile(MainFile);
    Token *end_tok = nullptr;
    TimeMeasuring t(true);
    Token *tok = Tokenizer(*this).tokenize(buffer, &end_tok);
    llvm::errs() << "Tokenize time: " << t.getTimeFromBeginning() << "ms\n";
    while (tok) {
        llvm::errs() << TokenNames[tok->id] << ":" << tok->name() << " ";
       tok = tok->next;
   }
}

const char* Compilation::readFile(const std::string &filepath) {   
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file_or_err = llvm::MemoryBuffer::getFile(filepath);

    if (std::error_code err = file_or_err.getError()) {
        llvm::WithColor::error(llvm::errs()) << "Error reading " << filepath << ": " << err.message() << "\n";
        exit(EXIT_FAILURE);
    }

    auto id = SrcMgr.AddNewSourceBuffer(std::move(*file_or_err), llvm::SMLoc());
    return SrcMgr.getMemoryBuffer(id)->getBuffer().begin();
}