/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_DIAGNOSTIC_H
#define RED_SYSTEM_DIAGNOSTIC_H

#include <llvm/Support/SourceMgr.h>

enum {
    #define DIAG(ID, Level, Msg) ID,
    #include "diagnostic.def"
};

const char * getDiagnosticText(unsigned DiagID);

llvm::SourceMgr::DiagKind getDiagnosticKind(unsigned DiagID);

#endif /* RED_SYSTEM_DIAGNOSTIC_H */
