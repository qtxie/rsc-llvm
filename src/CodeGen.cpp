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

static const char * const NodeName[] = {
    #define NODE(ID, STR) STR,
    #define TYPE(ID, STR) STR,
    #include "NodeId.def"
    nullptr
};

static const char * const TypeName[] {
    #define NODE(ID, STR)
    #define TYPE(ID, STR) STR,
    #include "NodeId.def"
    nullptr
};

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

llvm::Value * CodeGen::readVariable(Variable *var) {
    llvm::errs() << "readVariable: " << var->tok->str() << " " << var->ir << '\n';
    return builder->CreateLoad(var->ir);
}


void CodeGen::emitGlobalVar(Variable *var) {
    auto v = new llvm::GlobalVariable(
        *M, llvmType(var->type), false, GlobalValue::PrivateLinkage, nullptr, var->tok->str());
    var->ir = v;
}

llvm::Value * CodeGen::emitIf(If *node) {
    auto if_bb = createBasicBlock("if.body");
    auto after_if_bb = createBasicBlock("after.if");

    llvm::Value *cond = emitExpr(node->cond);
    builder->CreateCondBr(cond, if_bb, after_if_bb);

    setCurrentBasicBlock(if_bb);
    emitCode(node->body);
    if (!curBB->getTerminator())
        builder->CreateBr(after_if_bb);
    setCurrentBasicBlock(after_if_bb);
    return nullptr;
}

llvm::Value * CodeGen::emitCall(FnCall *call) {
    llvm::Function *func = llvm::cast<llvm::Function>(call->fn->ir);

    // process arguments
    llvm::SmallVector<llvm::Value *> argv;
    auto args = call->args;
    while (args) {
        argv.push_back(emitExpr(args));
        args = args->next;
    }

    auto call_inst = llvm::CallInst::Create(func->getFunctionType(), func, argv, "");
    builder->Insert(call_inst);
    return call_inst;
}

llvm::Value * CodeGen::emitBinOp(Node *node) {
    if (node->id < NodeAdd || node->id > NodeShiftRightLogical)
        return nullptr;

    BinaryOp *bin_op = (BinaryOp *)node;
    llvm::Value *left = emitExpr(bin_op->lhs);
    llvm::Value *right = emitExpr(bin_op->rhs);
    assert(left != nullptr && right != nullptr);
    llvm::Value *result = nullptr; 
    switch (node->id)
    {
    case NodeAdd:    result = builder->CreateNSWAdd(left, right); break;
    case NodeSub:    result = builder->CreateNSWSub(left, right); break;
    case NodeLess:   result = builder->CreateICmpSLT(left, right); break; 
    case NodeLessEq: result = builder->CreateICmpSLE(left, right); break; 
    default:
        break;
    }
    return result;
}

llvm::Value * CodeGen::emitExpr(Node *node) {
    llvm::errs() << "emitExpr: " << NodeName[node->id] << '\n';
    llvm::Value *val = nullptr;
    switch (node->id)
    {
    case NodeIf:     val = emitIf((If *)node); break;
    case NodeFnCall: val = emitCall((FnCall *)node); break;
    case NodeVar:    val = readVariable((Variable *)node); break;
    case NodeIdent:  val = readVariable(((Identifier *)node)->var); break;
    case TypeInt:    val = llvm::ConstantInt::get(i32_ty, ((Type *)node)->data.value); break;
    case TypeArray:  val = builder->CreateGlobalStringPtr(node->tok->str(), "", 0, M); break;
    default:         val = emitBinOp(node); break;
    }
    curVal = val;
    return val;
}

void CodeGen::emitCode(Node *node) {
    while (node) {
        llvm::Value *inst = nullptr;
        llvm::errs() << "emitCode " << NodeName[node->id] << '\n';
        switch (node->id)
        {
        case NodeReturn: builder->CreateRet(emitExpr(((Return *)node)->expr)); break;
        default:
            emitExpr(node);
            break;
        }
        node = node->next;
    }
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
    return llvm::FunctionType::get(result_ty, params_ty, fn->attrs.value & FuncVariadic);
}

void CodeGen::emitFunction(Function *fn) {
    auto func_ty = createFuncType(fn);
    auto func = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, fn->import_name->str(), *M);
    fn->ir = curFunc = func;

    if (auto lib = fn->data.import_lib) import_libs.insert(lib->str());

    // process attributes
    auto attrs = fn->attrs.value;
    if (attrs & FuncStdcall)
        func->setCallingConv(llvm::CallingConv::X86_StdCall);
    else if (attrs & FuncVariadic || attrs & FuncCdecl)
        func->setCallingConv(llvm::CallingConv::C);
    else
        func->setCallingConv(llvm::CallingConv::Fast);

    if (fn->data.import_lib) return;

    auto entry_block = BasicBlock::Create(Ctx, "entry", func);
    setCurrentBasicBlock(entry_block);

    auto alloca_on_stack = [this](Node *node) {
        Node head{};
        Node *cur = &head;
        while (node) {
            Variable *var = (Variable *) node;
            var->ir = builder->CreateAlloca(llvmType(var->type));
            cur = cur->next = var;
            node = node->next;
        }
    };

    // allocate args on stack
    alloca_on_stack(fn->params);
    Variable *var = (Variable *)fn->params;
    for (auto &arg : func->args()) {
        builder->CreateStore(&arg, var->ir);
        var = (Variable *)var->next;
    }
    // allocate local variables on stack
    alloca_on_stack(fn->locals);

    emitCode(fn->body.node);
    builder->CreateRet(curVal);
}

void CodeGen::emitMainFunc(Node *ast) {
    auto func_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
    auto func = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, comp.entryFuncName, *M);

    auto entry_block = BasicBlock::Create(Ctx, "entry", func);
    setCurrentBasicBlock(entry_block);
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
    OptimizationLevel opt_level = comp.optLevel;

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
    if (comp.emitIr) {
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
    if (comp.emitIr) {
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
    case TypeInt:    return i32_ty;
    case TypeDouble: return f64_ty;
    case TypeFloat:  return f32_ty;
    case TypeBool:   return bool_ty;
    case TypeByte:   return byte_ty;
    case TypePtr:    return llvmType(ty->data.type)->getPointerTo();
    case TypeArray:  return llvm::ArrayType::get(byte_ty, ty->tok->len);
    case TypeFunc: 

    default:
        break;
    }
    return nullptr;
}
