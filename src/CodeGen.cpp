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
    inLoop = false;
    in_catch = false;
    in_main_func = false;
}

llvm::Value * CodeGen::readVariable(Variable *var) {
    LOG() << var << " " << var->tok->str() << " " << var->ir << " " << var->attrs << '\n';
    return builder->CreateLoad(var->ir);
}

llvm::Value * CodeGen::getPointer(Node *node) {
    Variable *var = (Variable *)node;
    return var->ir;
}

llvm::GlobalVariable * CodeGen::emitGlobalVar(Variable *var) {
    auto v = new llvm::GlobalVariable(
        *M, llvmType(var->type), false, GlobalValue::PrivateLinkage, nullptr, var->tok->str());
    var->ir = v;
    var->attrs = static_cast<VarAttr>(var->attrs | VarGlobal);
    LOG() << var << '\n';
    return v;
}

llvm::Value * CodeGen::emitCase(Case *node) {
    return emitEither(node->cases);
}

llvm::Value * CodeGen::emitSwitch(Switch *node) {
    auto default_bb = createBasicBlock("switch.default");
    auto after_switch = createBasicBlock("after.switch");
    auto switch_op = builder->CreateSwitch(emitExpr(node->value), default_bb, node->cases->size());

    for (auto &[val, body] : *node->cases) {
        auto dest_bb = createBasicBlock();
        setCurrentBasicBlock(dest_bb);
        emitCode(body);
        if (!curBB->getTerminator())
            builder->CreateBr(after_switch);
        switch_op->addCase(getConstantInt(val), dest_bb);
    }
    
    setCurrentBasicBlock(default_bb);
    if (node->defaultCase) {
        emitCode(node->defaultCase);
        if (!curBB->getTerminator())
            builder->CreateBr(after_switch);
    } else {
        //TBD: throw an exception
    }

    setCurrentBasicBlock(after_switch);
    return nullptr;
}

llvm::Value * CodeGen::emitEither(Either *node) {
    auto if_then = createBasicBlock("if.then");
    auto if_else = createBasicBlock("if.else");
    auto after_if = createBasicBlock("after.if");

    llvm::Value *cond = emitExpr(node->cond);
    builder->CreateCondBr(cond, if_then, if_else);

    setCurrentBasicBlock(if_then);
    emitCode(node->then_body);
    if (!curBB->getTerminator())
        builder->CreateBr(after_if);

    setCurrentBasicBlock(if_else);
    if (node->else_body) emitCode(node->else_body);
    if (!curBB->getTerminator())
        builder->CreateBr(after_if);

    setCurrentBasicBlock(after_if);
    return nullptr;
}

void CodeGen::emitIf(If *node) {
    auto if_bb = createBasicBlock("if.body");
    auto after_if_bb = createBasicBlock("after.if");

    llvm::Value *cond = emitExpr(node->cond);
    builder->CreateCondBr(cond, if_bb, after_if_bb);

    setCurrentBasicBlock(if_bb);
    emitCode(node->body);
    if (!curBB->getTerminator())
        builder->CreateBr(after_if_bb);
    setCurrentBasicBlock(after_if_bb);
}

void CodeGen::emitBreak(Node *node) {
    if (inLoop) {
        builder->CreateBr(breakBB);
    } else {
        error(node, err_no_loop, "BREAK");
    }
}

void CodeGen::emitContinue(Node *node) {
    if (inLoop) {
        builder->CreateBr(continueBB);
    } else {
        error(node, err_no_loop, "CONTINUE");
    }
}

void CodeGen::emitLoop(Loop *node) {
    auto in_loop = inLoop;
    auto conti_bb = continueBB;
    auto break_bb = breakBB;

    auto counter = builder->CreateAlloca(i32_ty);
    builder->CreateStore(emitExpr(node->counter), counter);

    auto *loop_cond = createBasicBlock("loop.cond");
    auto *loop_body = createBasicBlock("loop.body");
    auto after_loop = createBasicBlock("after.loop");

    builder->CreateBr(loop_cond);
    setCurrentBasicBlock(loop_cond);
    auto cond = builder->CreateICmpEQ(builder->CreateLoad(counter), builder->getInt32(0));
    builder->CreateCondBr(cond, after_loop, loop_body);

    setCurrentBasicBlock(loop_body);
    auto new_val = builder->CreateNUWSub(builder->CreateLoad(counter), builder->getInt32(1));
    builder->CreateStore(new_val, counter);

    continueBB = loop_cond;
    breakBB = after_loop;
    inLoop = true;
    emitCode(node->body);
    inLoop = in_loop;
    breakBB = break_bb;
    continueBB = conti_bb;

    if (!curBB->getTerminator()) builder->CreateBr(loop_cond);
    setCurrentBasicBlock(after_loop);
}

void CodeGen::emitUntil(Until *node) {
    auto in_loop = inLoop;
    auto conti_bb = continueBB;
    auto break_bb = breakBB;

    llvm::BasicBlock *until_body;
    auto after_until = createBasicBlock("after.until");

    if (curBB->empty()) {
        curBB->setName("until.body");
        until_body = curBB;
    } else {
        until_body = createBasicBlock("until.body");
        builder->CreateBr(until_body);
        setCurrentBasicBlock(until_body);
    }

    continueBB = until_body;
    breakBB = after_until;
    inLoop = true;

    llvm::Value *cond = emitCode(node->body);
    builder->CreateCondBr(cond, after_until, until_body);

    inLoop = in_loop;
    breakBB = break_bb;
    continueBB = conti_bb;

    setCurrentBasicBlock(after_until);
}

void CodeGen::emitWhile(While *node) {
    auto in_loop = inLoop;
    auto conti_bb = continueBB;
    auto break_bb = breakBB;

    llvm::BasicBlock *while_cond;
    auto while_body = createBasicBlock("while.body");
    auto after_while = createBasicBlock("after.while");

    if (curBB->empty()) {
        curBB->setName("while.cond");
        while_cond = curBB;
    } else {
        while_cond = createBasicBlock("while.cond");
        builder->CreateBr(while_cond);
        setCurrentBasicBlock(while_cond);
    }

    llvm::Value *cond = emitCode(node->cond);
    builder->CreateCondBr(cond, while_body, after_while);

    setCurrentBasicBlock(while_body);

    continueBB = while_cond;
    breakBB = after_while;
    inLoop = true;
    emitCode(node->body);
    inLoop = in_loop;
    breakBB = break_bb;
    continueBB = conti_bb;

    if (!curBB->getTerminator()) builder->CreateBr(while_cond);
    setCurrentBasicBlock(after_while);
}

void CodeGen::handleThrow() {
    auto if_bb = createBasicBlock("e.catched", afterCatchBB);
    auto after_if_bb = createBasicBlock("after.if", afterCatchBB);
    auto exception_val = in_main_func ? firstLocalVar : curFunc->getArg(0);
    llvm::Value *exception = builder->CreateLoad(exception_val);
    llvm::Value *filter = in_catch ? catchFilter : llvm::ConstantInt::get(i32_ty, 0);
    llvm::Value *cond = builder->CreateICmpSLE(exception, filter);
    builder->CreateCondBr(cond, if_bb, after_if_bb);

    auto saved_bb = curBB;
    setCurrentBasicBlock(if_bb);
    builder->CreateBr(afterCatchBB);
    setCurrentBasicBlock(after_if_bb);
}

llvm::Value * CodeGen::emitCall(FnCall *call) {
    LOG() << call->fn->tok->str() << '\n';
    llvm::Function *func = llvm::cast<llvm::Function>(call->fn->ir);

    // process arguments
    llvm::SmallVector<llvm::Value *> argv;
    auto args = call->args;
    while (args) {
        argv.push_back(emitExpr(args));
        args = args->next;
    }

    LOG() << call->tok->str() << " " << argv.size() << "\n";
    auto call_inst = llvm::CallInst::Create(func->getFunctionType(), func, argv, "");
    builder->Insert(call_inst);
    if (call->fn->isThrowable()) handleThrow();
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
    case NodeAdd:        result = builder->CreateNSWAdd(left, right); break;
    case NodeSub:        result = builder->CreateNSWSub(left, right); break;
    case NodeMul:        result = builder->CreateNSWMul(left, right); break;
    case NodeDiv:        result = builder->CreateSDiv(left, right); break;
    case NodeRemainder:  result = builder->CreateSRem(left, right); break;
    //case NodeModulo:     result = builder->Create(left, right); break;
    case NodeEq:         result = builder->CreateICmpEQ(left, right); break;
    case NodeNotEq:      result = builder->CreateICmpNE(left, right); break;
    case NodeLess:       result = builder->CreateICmpSLT(left, right); break;
    case NodeLessEq:     result = builder->CreateICmpSLE(left, right); break;
    case NodeGreater:    result = builder->CreateICmpSGT(left, right); break;
    case NodeGreaterEq:  result = builder->CreateICmpSGE(left, right); break;
    case NodeAnd:        result = builder->CreateAnd(left, right); break;
    case NodeOr:         result = builder->CreateOr(left, right); break;
    case NodeXor:        result = builder->CreateXor(left, right); break;
    case NodeShiftLeft:  result = builder->CreateShl(left, right); break;
    case NodeShiftRight: result = builder->CreateAShr(left, right); break;
    case NodeShiftRightLogical: result = builder->CreateLShr(left, right); break;
    default:
        assert(false);
        break;
    }
    return result;
}

llvm::Value * CodeGen::emitExpr(Node *node) {
    LOG << NodeName[node->id] << '\n';

    llvm::Value *val = nullptr;
    switch (node->id)
    {
    case NodeEither: val = emitEither((Either *)node); break;
    case NodeSwitch: val = emitSwitch((Switch *)node); break;
    case NodeCase:   val = emitCase((Case *)node); break;
    case NodeFnCall: val = emitCall((FnCall *)node); break;
    case NodeVar:    val = readVariable((Variable *)node); break;
    case NodeIdent:  val = readVariable(((Identifier *)node)->var); break;
    case NodePtr:    val = getPointer(((Pointer *)node)->value); break;
    case TypeInt:    val = getConstantInt(node); break;
    case TypeCStr:   val = builder->CreateGlobalStringPtr(node->tok->str(), "", 0, M); break;
    default:         val = emitBinOp(node); break;
    }
    curVal = val;
    return val;
}

llvm::Value * CodeGen::emitAssignment(Assignment *node) {
    Variable *left = (Variable *)node->lhs;
    LOG() << left->tok->str() << left->ir << '\n';
    llvm::Value *val = nullptr;
    if (node->rhs->id == NodeAssign) {
        val = emitAssignment((Assignment *)node->rhs);
    } else {
        val = emitExpr(node->rhs);
    }

    if (left->ir) {  // assingment
        builder->CreateStore(val, left->ir);
    } else {         // declare global variable
        auto var = emitGlobalVar(left);
        if (llvm::isa<llvm::Constant>(val))
            var->setInitializer(llvm::cast<llvm::Constant>(val));
    }
    return val;
}

void CodeGen::emitThrow(Throw *node) {
    auto val = emitExpr(node->value);
    auto arg_throw = curFunc->getArg(0);
    builder->CreateStore(val, arg_throw);
    builder->CreateRetVoid();
}

void CodeGen::emitCatch(Catch *node) {
    in_catch = true;
    catchFilter = emitExpr(node->filter);
    afterCatchBB = createBasicBlock("after.catch");
    emitCode(node->body);
    builder->CreateBr(afterCatchBB);
    setCurrentBasicBlock(afterCatchBB);
    in_catch = false;
}

llvm::Value * CodeGen::emitCode(Node *node) {
    llvm::Value *val = nullptr;
    while (node) {
        llvm::Value *inst = nullptr;
        LOG() << NodeName[node->id] << '\n';
        switch (node->id)
        {
        case NodeIf:     emitIf((If *)node); break;
        case NodeLoop:   emitLoop((Loop *)node); break;
        case NodeUntil:  emitUntil((Until *)node); break;
        case NodeWhile:  emitWhile((While *)node); break;
        case NodeBreak:  emitBreak(node); break;
        case NodeContinue: emitContinue(node); break;
        case NodeReturn: builder->CreateRet(emitExpr(((Return *)node)->expr)); break;
        case NodeThrow: emitThrow((Throw *)node); break;
        case NodeCatch: emitCatch((Catch *)node); break;
        case NodeAssign: val = emitAssignment((Assignment *)node); break;
        default:
            val = emitExpr(node);
            break;
        }
        node = node->next;
    }
    return val;
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
            LOG() << fn->tok->str() << " " << params_ty.size() << "\n";
    return llvm::FunctionType::get(result_ty, params_ty, fn->attrs & FuncVariadic);
}


void CodeGen::mangleName(Token *tok, Context *ctx, std::string &result) {
    result = tok->str().str();
    while (ctx->parent) {
        result.insert(result.begin(), '/');
        result.insert(0, ctx->tok->loc, ctx->tok->len);
        ctx = ctx->parent;
    }
}

llvm::Value * CodeGen::createFunc(Function *fn, Context *ctx) {
    auto func_ty = createFuncType(fn);
    std::string name;
    mangleName(fn->import_name, ctx, name);
    auto func = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, name, *M);
    fn->ir = func;

    if (auto lib = fn->data.import_lib) import_libs.insert(lib->str());

    // process attributes
    auto attrs = fn->attrs;
    if (attrs & FuncStdcall)
        func->setCallingConv(llvm::CallingConv::X86_StdCall);
    else if (attrs & FuncVariadic || attrs & FuncCdecl)
        func->setCallingConv(llvm::CallingConv::C);
    else
        func->setCallingConv(llvm::CallingConv::Fast);

    return func;
}

void CodeGen::createFunctions(Context *ctx) {
    for (auto const &val : ctx->decls) {
        Type *ty = (Type *)val.second;
        if (ty->id == TypeFunc) {
            createFunc((Function *)ty, ctx);
        } else if (ty->id == NodeContext) {
            createFunctions((Context* )ty);
        }
    }
}

void CodeGen::emitFunc(Function *fn) {
    if (fn->data.import_lib) return;

    llvm::Function *func = llvm::cast<llvm::Function>(fn->ir);
    curFunc = func;
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
    if (in_main_func && fn->isThrowable()) {
        afterCatchBB = createBasicBlock("after.catch");
        firstLocalVar = ((Variable *)fn->locals)->ir;
    }

    emitCode(fn->body.node);
    if (!curBB->getTerminator()) {
        if (fn->return_type)
            builder->CreateRet(curVal);
        else
            builder->CreateRetVoid();
    }
}

void CodeGen::emitFunctions(Context *ctx) {
    for (auto const &val : ctx->decls) {
        Type *ty = (Type *)val.second;
        if (ty->id == TypeFunc) {
            LOG() << ty->tok->str() << '\n';
            emitFunc((Function *)ty);
        } else if (ty->id == NodeContext) {
            emitFunctions((Context* )ty);
        }
    }
}

void CodeGen::emitMainFunc(Function *fn) {
    LOG() << '\n';
    auto func_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
    fn->ir = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, comp.entryFuncName, *M);
    in_main_func = true;
    emitFunc(fn);
    if (fn->isThrowable() && !afterCatchBB->getTerminator()) {
        setCurrentBasicBlock(afterCatchBB);
        builder->CreateRetVoid();
    }
    in_main_func = false;
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
        filepath.append(OBJ_EXT);
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

void CodeGen::emit(Context *ctx) {
    createFunctions(ctx);
    emitMainFunc(ctx->func);
    emitFunctions(ctx);
    output();
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
    case TypeCStr:   return byte_ty->getPointerTo();
    //case TypeArray:  return llvm::ArrayType::get(byte_ty, ty->tok->len);
    case TypeFunc: break; 
    case TypeVoid:
            error(node, err_not_initialized, node->tok->str());
    default:
        break;
    }
    return nullptr;
}

template <typename... Args>
void CodeGen::error(const Node *node, unsigned diag_id, Args &&... args) {
    comp.error(node->tok, diag_id, std::forward<Args>(args)...);
}
