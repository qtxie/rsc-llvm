/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "diagnostic.h"

using llvm::SourceMgr;

namespace {
    const char *DiagnosticText[] = {
        #define DIAG(ID, Level, Msg) Msg,
        #include "diagnostic.def"
    };
    llvm::SourceMgr::DiagKind DiagnosticKind[] = {
        #define DIAG(ID, Level, Msg) SourceMgr::DK_##Level,
        #include "diagnostic.def"
    };
}

const char * getDiagnosticText(unsigned DiagID) {
  return DiagnosticText[DiagID];
}

llvm::SourceMgr::DiagKind getDiagnosticKind(unsigned DiagID) {
  return DiagnosticKind[DiagID];
}