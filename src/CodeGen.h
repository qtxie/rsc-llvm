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
#include <llvm/ADT/SmallSet.h>

class Compilation;

class CodeGen
{
private:
    Compilation &comp;
    llvm::LLVMContext Ctx;
    llvm::Module *M;
    llvm::IRBuilder<> *builder;
    llvm::Function *curFunc;
    llvm::BasicBlock *curBB;
    llvm::Value *curVal;

    llvm::IntegerType *bool_ty;
    llvm::IntegerType *byte_ty;
    llvm::IntegerType *i32_ty;
    llvm::Type *f32_ty;
    llvm::Type *f64_ty;

    Logger LOG;

public:
    CodeGen(Compilation &c);
    ~CodeGen() {delete M;}
    CodeGen(Compilation &&) = delete;  // prevents rvalue binding

    llvm::Value * readVariable(Variable *var);
    llvm::Value * getPointer(Node *node);

    llvm::Value * emitIf(If *node);
    llvm::Value * emitCall(FnCall *call);
    llvm::Value * emitBinOp(Node *node);
    llvm::Value * emitExpr(Node *node);
    llvm::Value * emitAssignment(Assignment *node);
    llvm::GlobalVariable * emitGlobalVar(Variable *var);

    void emitCode(Node *node);
    void emitMainFunc(Node *ast);
    void emitFunc(Function *fn);
    void emitFunctions(Context *ctx);

    llvm::Value * createFunc(Function *fn, Context *ctx);

    void output();
    void emit(Program *prog);

    llvm::SmallSet<StringRef, 4> import_libs;

private:
    void mangleName(Token *tok, Context *ctx, std::string &result);

    llvm::BasicBlock * createBasicBlock(const llvm::Twine &name, llvm::BasicBlock *insertBefore = nullptr) {
        return llvm::BasicBlock::Create(Ctx, name, curFunc, insertBefore);
    }
    void setCurrentBasicBlock(llvm::BasicBlock *bb) {
        curBB = bb;
        builder->SetInsertPoint(bb);
    }
    llvm::FunctionType * createFuncType(Function *fn);
    llvm::Type * llvmType(const Node *ty);
};

#endif /* RED_SYSTEM_CODEGEN_H */
