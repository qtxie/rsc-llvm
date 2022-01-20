/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "CodeGen.h"
#include "Compilation.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ToolOutputFile.h>

using llvm::GlobalValue;
using llvm::BasicBlock;

CodeGen::CodeGen(Compilation &c) : comp(c) {
    M = new llvm::Module(c.mainFile, Ctx);
    M->setTargetTriple(c.TM->getTargetTriple().getTriple());
    M->setDataLayout(c.TM->createDataLayout());
    builder = new llvm::IRBuilder<>(Ctx);
    i32_ty = llvm::Type::getInt32Ty(Ctx);
    f32_ty = llvm::Type::getFloatTy(Ctx);
    f64_ty = llvm::Type::getDoubleTy(Ctx);
    bool_ty = llvm::Type::getInt1Ty(Ctx);
    byte_ty = llvm::Type::getInt8Ty(Ctx);
}

llvm::FunctionType * CodeGen::createFuncType(Function *fn) {
    auto result_ty = llvm::Type::getVoidTy(Ctx);
    if (fn->return_type)
        result_ty = llvmType(fn->return_type);

    llvm::SmallVector<llvm::Type *> params_ty;
    auto params = fn->params;
    while (params) {
        params_ty.push_back(llvmType(params));
        params = params->next;
    }
    return llvm::FunctionType::get(result_ty, params_ty, false);
}

void CodeGen::emitGlobalVar(Variable *var) {
    auto v = new llvm::GlobalVariable(
        *M, llvmType(var->type), false, GlobalValue::PrivateLinkage, nullptr, var->tok->str());
    var->ir = v;
}

void CodeGen::emitCall(FnCall *call) {
    llvm::Function *func = llvm::cast<llvm::Function>(call->fn->ir);
    llvm::SmallVector<llvm::Value *> argv;
    auto args = call->args;
    while (args) {
        argv.push_back(emitExpr(args));
        args = args->next;
    }

    auto call_inst = llvm::CallInst::Create(func->getFunctionType(), func, argv, "");
    builder->Insert(call_inst);
}

llvm::Value * CodeGen::emitExpr(Node *node) {
    switch (node->id)
    {
    case TypeInt: return llvm::ConstantInt::get(i32_ty, ((Type *)node)->data.value);
    default:
        break;
    }
    return nullptr;
}

void CodeGen::emitCode(Node *node) {
    while (node) {
        switch (node->id)
        {
        case NodeFnCall: emitCall((FnCall *)node); break;
        case NodeReturn: builder->CreateRet(emitExpr(node)); break;
        default:
            //builder->Insert(emitExpr(node));
            break;
        }
        node = node->next;
    }
}

void CodeGen::emitFunction(Function *fn) {
    auto func_ty = createFuncType(fn);
    auto func = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, fn->tok->str(), *M);
    fn->ir = func;

    auto entry_block = BasicBlock::Create(Ctx, "entry", func);
    builder->SetInsertPoint(entry_block);

    // allocate local variables
    auto locals = (Variable *)fn->locals;
    while (locals) {
        locals->ir = builder->CreateAlloca(llvmType(locals->type));
        locals = (Variable *)locals->next;
    }

    emitCode(fn->body.node);
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));
}

void CodeGen::emitMainFunc(Node *ast) {
    auto func_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
    auto func = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, comp.entryFuncName, *M);

    auto entry_block = BasicBlock::Create(Ctx, "entry", func);
    builder->SetInsertPoint(entry_block);
    emitCode(ast);
    builder->CreateRetVoid();
}

void CodeGen::output() {
    using namespace llvm;
    using OptimizationLevel = typename PassBuilder::OptimizationLevel;

    PassBuilder pass_builder(comp.TM);
    LoopAnalysisManager loop_am;
    FunctionAnalysisManager func_am;
    CGSCCAnalysisManager cgscc_am;
    ModuleAnalysisManager module_am;

    func_am.registerPass([&] { return pass_builder.buildDefaultAAPipeline(); });
    //auto tlii = std::make_unique<TargetLibraryInfoImpl>(comp.TM->getTargetTriple());
    //func_am.registerPass([&] { return TargetLibraryAnalysis(*tlii); });

    // Initialize the AnalysisManagers
    pass_builder.registerModuleAnalyses(module_am);
    pass_builder.registerCGSCCAnalyses(cgscc_am);
    pass_builder.registerFunctionAnalyses(func_am);
    pass_builder.registerLoopAnalyses(loop_am);
    pass_builder.crossRegisterProxies(loop_am, func_am, cgscc_am, module_am);

    pass_builder.registerPipelineStartEPCallback(
        [](ModulePassManager &module_pm, OptimizationLevel OL) {
            module_pm.addPass(VerifierPass());
        });
    
    ModulePassManager module_pm;
    OptimizationLevel opt_level;

    // Setting up the optimization level
    opt_level = OptimizationLevel::O0;

    // Initialize the PassManager
    if (opt_level == OptimizationLevel::O0) {
      module_pm = pass_builder.buildO0DefaultPipeline(opt_level);
    } else {
      module_pm = pass_builder.buildPerModuleDefaultPipeline(opt_level);
    }

    legacy::PassManager codegen_pm;
    codegen_pm.add(createTargetTransformInfoWrapperPass(comp.TM->getTargetIRAnalysis()));

    std::error_code err;
    sys::fs::OpenFlags open_flags = sys::fs::OF_None;

    auto filepath = comp.outputFilepath;
    if (comp.emit_llvm_ir) {
        open_flags |= sys::fs::OF_Text;
        filepath.append(".ll");
    } else {
        filepath.append(".obj");
    }

    // Open the file.
    auto out_file = std::make_unique<llvm::ToolOutputFile>(filepath, err, open_flags);
    if (err) {
        WithColor::error(errs()) << err.message() << '\n';
        return;
    }
    if (comp.emit_llvm_ir) {
        codegen_pm.add(createPrintModulePass(out_file->os()));
    } else {
        comp.TM->addPassesToEmitFile(codegen_pm, out_file->os(), nullptr, CGFT_ObjectFile);
    }

    // Optimization phase
    module_pm.run(*M, module_am);

    // Code generation phase
    codegen_pm.run(*M);
    out_file->keep();
}

llvm::Type * CodeGen::llvmType(const Node *node) {
    const Type *ty = (const Type *)node;
    if (node->id == NodeVar)
        ty = ((const Variable *)node)->type;
    switch (ty->id)
    {
    case TypeInt: return i32_ty;
    case TypeDouble: return f64_ty;
    case TypeFloat: return f32_ty;
    case TypeBool: return bool_ty;
    case TypeByte: return byte_ty;
    case TypeFunc: 

    default:
        break;
    }
    return nullptr;
}
