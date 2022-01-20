/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_CODEGEN_H
#define RED_SYSTEM_CODEGEN_H

#include "Parser.h"
#include <llvm/IR/IRBuilder.h>

class Compilation;

class CodeGen
{
private:
    Compilation &comp;
    llvm::LLVMContext Ctx;
    llvm::Module *M;
    llvm::IRBuilder<> *builder;

    llvm::IntegerType *bool_ty;
    llvm::IntegerType *byte_ty;
    llvm::IntegerType *i32_ty;
    llvm::Type *f32_ty;
    llvm::Type *f64_ty;

public:
    CodeGen(Compilation &c);
    ~CodeGen() {delete M;}
    CodeGen(Compilation &&) = delete;  // prevents rvalue binding

    llvm::Value * emitExpr(Node *node);

    void emitCode(Node *node);
    void emitCall(FnCall *call);
    void emitGlobalVar(Variable *var);
    void emitFunction(Function *fn);
    void emitMainFunc(Node *ast);

    void output();

private:
    llvm::FunctionType * createFuncType(Function *fn);
    llvm::Type * llvmType(const Node *ty);
};

#endif /* RED_SYSTEM_CODEGEN_H */
