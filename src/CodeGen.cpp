/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "CodeGen.h"
#include "Compilation.h"
#include "Parser.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/WithColor.h>

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

static int getCallingConv(FuncAttr attr) {
    if (attr & FuncStdcall)
        return llvm::CallingConv::X86_StdCall;
    else if (attr & FuncVariadic || attr & FuncCdecl)
        return llvm::CallingConv::C;
    else
        return llvm::CallingConv::Fast;
}

CodeGen::CodeGen(Compilation &c) : comp(c) {
    M = new llvm::Module(c.mainFile, Ctx);
    M->setTargetTriple(c.TM->getTargetTriple().getTriple());
    M->setDataLayout(c.TM->createDataLayout());
    builder = new llvm::IRBuilder<>(Ctx);
    i32_ty = llvm::Type::getInt32Ty(Ctx);
    i64_ty = llvm::Type::getInt64Ty(Ctx);
    f32_ty = llvm::Type::getFloatTy(Ctx);
    f64_ty = llvm::Type::getDoubleTy(Ctx);
    bool_ty = llvm::Type::getInt1Ty(Ctx);
    byte_ty = llvm::Type::getInt8Ty(Ctx);
    inLoop = false;
    in_catch = false;
}

llvm::Value * CodeGen::readStructMember(Path *path) {
    return CreateLoad(getElementPtr(path));
}

llvm::Value * CodeGen::readPointerValue(Path *path) {
    return CreateLoad(getElementPtr(path));
}

llvm::Value * CodeGen::getElementPtr(Path *path) {
    assert(path->first->id == NodeVar);
    
    llvm::SmallVector<llvm::Value *> indices;
    if (path->id == NodeMemberAccess)
        indices.push_back(llvm::ConstantInt::get(i32_ty, 0));

    Variable *var = (Variable *)path->first;
    auto var_ty = var->type;
    LOG() << var->tok->str() << ' ' << TypeName[var->type->id] << '\n';

    llvm::Type *ty;
    llvm::Value *ptr;
    if (var_ty->id == TypeStruct || (var->hasAttr(VarGlobal) && var_ty->id == TypePtr)) {
        ptr = var->ir;
        ty = ptr->getType();
        if (ty->getPointerElementType()->isArrayTy()) {
            ptr = getArrayPtr(llvm::cast<llvm::GlobalVariable>(ptr));
            ty = ptr->getType();
        }
    } else {
        ptr = CreateLoad(var->ir);
        ty = ptr->getType();
    }
    ty = ty->getPointerElementType();

    Unary *node = (Unary *)path->value;
    NodeId type_id;
    while (node) {
        LOG() << node->tok->str() << '\n';

        type_id = node->id;
        if (type_id == NodeMemberAccess) {
            Member *m = (Member *)node->value;
            type_id = m->type->id;
            indices.push_back(llvm::ConstantInt::get(i32_ty, m->idx));
        } else if (type_id == NodeIdent) {
            auto idx = readVariable(((Identifier *)node)->var);
            idx = builder->CreateNSWSub(idx, llvm::ConstantInt::get(i32_ty, 1));
            indices.push_back(idx);
        } else if (type_id == TypeInt) {
            Type *t = (Type *)node;
            indices.push_back(llvm::ConstantInt::get(i32_ty, t->data.value - 1));
        }

        if (type_id == TypeCStr || type_id == TypePtr) {
            ptr = builder->CreateGEP(ty, ptr, indices);
            if (node->next) {
                ptr = CreateLoad(ptr);
                ty = ptr->getType()->getScalarType()->getPointerElementType();
                indices.clear();
                if (node->next->id == NodeMemberAccess)
                    indices.push_back(llvm::ConstantInt::get(i32_ty, 0));
            } else {
                return ptr;
            }
        }
        node = (Unary *)node->next;
    };

    return builder->CreateGEP(ty, ptr, indices);
}

llvm::Value * CodeGen::readVariable(Variable *var) {
    LOG() << var << " " << var->tok->str() << " " << var->ir << " " << var->attrs << '\n';
    if (var->type->id == TypePtr && var->hasAttr(VarGlobal)) return var->ir;
    return CreateLoad(var->ir);
}

llvm::Value * CodeGen::getPointer(Node *node) {
    if (node->id == NodeVar) {
        Variable *var = (Variable *)node;
        return var->ir;
    } else if (node->id == TypeFunc) {
        return ((Function *)node)->ir;
    }
    llvm_unreachable("");
}

llvm::GlobalVariable * CodeGen::emitGlobalVar(Variable *var, const Type *ty) {
    auto v = new llvm::GlobalVariable(
        *M, llvmType(ty), false, GlobalValue::PrivateLinkage, nullptr, var->tok->str());
    var->ir = v;
    var->addAttr(VarGlobal);
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

    auto result = node->result;

    for (auto &[val, body] : *node->cases) {
        auto dest_bb = createBasicBlock();
        setCurrentBasicBlock(dest_bb);
        auto res = emitCode(body);
        if (result) builder->CreateStore(res, result->ir);
        if (!curBB->getTerminator())
            builder->CreateBr(after_switch);
        switch_op->addCase(getConstantInt(val), dest_bb);
    }
    
    setCurrentBasicBlock(default_bb);
    if (node->defaultCase) {
        auto res = emitCode(node->defaultCase);
        if (result) builder->CreateStore(res, result->ir);
        if (!curBB->getTerminator())
            builder->CreateBr(after_switch);
    } else {
        //TBD: throw an exception?
        builder->CreateBr(after_switch);
    }

    setCurrentBasicBlock(after_switch);
    return result ? CreateLoad(result->ir) : nullptr;
}

llvm::Value * CodeGen::emitEither(Either *node) {
    auto if_then = createBasicBlock("if.then");
    auto if_else = createBasicBlock("if.else");
    auto after_if = createBasicBlock("after.if");

    auto result = node->result;

    llvm::Value *cond = emitExpr(node->cond);
    builder->CreateCondBr(cond, if_then, if_else);

    setCurrentBasicBlock(if_then);
    auto val = emitCode(node->then_body);
    if (val && result) builder->CreateStore(val, result->ir);
    if (!curBB->getTerminator())
        builder->CreateBr(after_if);

    setCurrentBasicBlock(if_else);
    if (node->else_body) {
        val = emitCode(node->else_body);
        if (val && result) builder->CreateStore(val, result->ir);
    }
    if (!curBB->getTerminator())
        builder->CreateBr(after_if);

    setCurrentBasicBlock(after_if);
    return result ? CreateLoad(result->ir) : nullptr;
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

    auto counter = loopCounter;
    builder->CreateStore(emitExpr(node->counter), counter);

    auto *loop_cond = createBasicBlock("loop.cond");
    auto *loop_body = createBasicBlock("loop.body");
    auto after_loop = createBasicBlock("after.loop");

    builder->CreateBr(loop_cond);
    setCurrentBasicBlock(loop_cond);
    auto cond = builder->CreateICmpEQ(CreateLoad(counter), builder->getInt32(0));
    builder->CreateCondBr(cond, after_loop, loop_body);

    setCurrentBasicBlock(loop_body);
    auto new_val = builder->CreateSub(CreateLoad(counter), builder->getInt32(1));
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

    llvm::BasicBlock *until_body = createBasicBlock("until.body");;
    auto after_until = createBasicBlock("after.until");
        
    builder->CreateBr(until_body);
    setCurrentBasicBlock(until_body);

    continueBB = until_body;
    breakBB = after_until;
    inLoop = true;

    llvm::Value *cond = emitCode(node->body);
    if (cond) {
        builder->CreateCondBr(cond, after_until, until_body);
    }

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
    if (cond) builder->CreateCondBr(cond, while_body, after_while);

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
    auto after_if_bb = createBasicBlock("after.if", afterCatchBB);
    auto catch_bb = createBasicBlock("catch", afterCatchBB);
    auto exception_val = curFunc->getArg(0);
    llvm::Value *exception = CreateLoad(exception_val);
    auto zero = llvm::ConstantInt::get(i32_ty, 0);
    builder->CreateCondBr(builder->CreateICmpEQ(exception, zero), after_if_bb, catch_bb);

    setCurrentBasicBlock(catch_bb);
    if (afterCatchBB) {     // has catch
        auto rethrow_bb = createBasicBlock("rethrow");
        llvm::Value *filter = in_catch ? catchFilter : zero; 
        llvm::Value *cond = builder->CreateICmpSLE(exception, filter);
        builder->CreateCondBr(cond, afterCatchBB, rethrow_bb);
        setCurrentBasicBlock(rethrow_bb);
    }

    if (returnVal) builder->CreateRet(CreateLoad(returnVal));
    else builder->CreateRetVoid();

    setCurrentBasicBlock(after_if_bb);
}

llvm::Value * CodeGen::emitCall(FnCall *call) {
    static const int typed_id[] = {
    // void null logic byte integer float32 float64 enum pointer struct
        0,   8,   1,    3,   2,      4,      5,     2,   8,       1000,
    // func c-str binary array
       9,    6,   10,    11
    };

    auto fn = call->fn;
    LOG() << NodeName[fn->id] << '\n';
    llvm::Value *fn_ir;
    FuncType *fn_ty;
    if (fn->id == TypeFunc) {
        fn_ty = fn->type;
        fn_ir = fn->ir;
    } else {
        auto var = (Variable *)fn;
        fn_ty = (FuncType *)var->type;
        fn_ir = CreateLoad(var->ir);
    }

    // process arguments
    llvm::SmallVector<llvm::Value *> argv;
    auto args = call->args;
    if (fn_ty->hasAttr(FuncTyped)) {
        argv.push_back(llvm::ConstantInt::get(i32_ty, call->n_args));   // count
        auto list_ptr_ty = llvmType(fn_ty->params->next);
        auto list_ty = list_ptr_ty->getPointerElementType();
        auto list = builder->CreateBitCast(typedValues, list_ptr_ty);
        argv.push_back(list);   // list
        llvm::Value * idx[2];
        int i = 0;
        while (args) {
            auto ty = parser->analyzeNodeType(args);
            int id = typed_id[ty->id];
            LOG() << id << '\n';
            if (id > 0) {
                idx[0] = llvm::ConstantInt::get(i32_ty, i++);
                idx[1] = llvm::ConstantInt::get(i32_ty, 0);
                auto val = emitExpr(args);
                builder->CreateStore(
                    llvm::ConstantInt::get(i32_ty, id),
                    builder->CreateGEP(list_ty, list, idx)
                );
                idx[1] = llvm::ConstantInt::get(i32_ty, 1);
                auto ptr = builder->CreateGEP(list_ty, list, idx);
                switch (id)
                {
                case 1: val = builder->CreateIntCast(val, i32_ty, false); break;
                case 3: val = builder->CreateIntCast(val, i32_ty, false); break;
                case 4: ptr = builder->CreateBitCast(ptr, f32_ty->getPointerTo()); break;
                case 5: ptr = builder->CreateBitCast(ptr, f64_ty->getPointerTo()); break;
                case 6: ptr = builder->CreateBitCast(ptr, byte_ty->getPointerTo()->getPointerTo()); break;
                case 8: ptr = builder->CreateBitCast(ptr, val->getType()->getPointerTo()); break;
                default:
                    break;
                }
                builder->CreateStore(val, ptr);
            } else {
                error(args, err_invalid_arg);
            }
            args = args->next;
        }
    } else {
        int n = 0;
        while (args) {
            auto v = emitExpr(args);
            if (args->id == NodeNull) {
                v = builder->CreateBitCast(v, fn_ty->ir->getParamType(n));
            } else if (v->getType()->isPointerTy() && (args->flags & NodeFlagStruct)) {
                v = CreateLoad(v);
            }
            argv.push_back(v);
            args = args->next;
            n++;
        }
    }

    LOG() << call->tok->str() << " " << argv.size() << ' ' << fn_ty->ir << ' ' << fn_ir << "\n";
    auto call_inst = builder->CreateCall(fn_ty->ir, fn_ir, argv);
    call_inst->setCallingConv(getCallingConv(fn_ty->attrs));

    if (call->fn->isThrowable()) handleThrow();
    return call_inst;
}

llvm::Value * CodeGen::emitBinOpInt(NodeId op, llvm::Value *left, llvm::Value *right, bool isSigned) {
    switch (op)
    {
    case NodeAdd:        return isSigned ? builder->CreateNSWAdd(left, right) : builder->CreateNUWAdd(left, right);
    case NodeSub:        return isSigned ? builder->CreateNSWSub(left, right) : builder->CreateNUWSub(left, right);
    case NodeMul:        return isSigned ? builder->CreateNSWMul(left, right) : builder->CreateNUWMul(left, right);
    case NodeDiv:        return isSigned ? builder->CreateSDiv(left, right) : builder->CreateUDiv(left, right);
    case NodeModulo:
    case NodeRemainder:  return isSigned ? builder->CreateSRem(left, right) : builder->CreateURem(left, right);
    // case NodeModulo:     return builder->Create(left, right);
    case NodeEq:         return builder->CreateICmpEQ(left, right);
    case NodeNotEq:      return builder->CreateICmpNE(left, right);
    case NodeLess:       return isSigned ? builder->CreateICmpSLT(left, right) : builder->CreateICmpULT(left, right);
    case NodeLessEq:     return isSigned ? builder->CreateICmpSLE(left, right) : builder->CreateICmpULE(left, right);
    case NodeGreater:    return isSigned ? builder->CreateICmpSGT(left, right) : builder->CreateICmpUGT(left, right);
    case NodeGreaterEq:  return isSigned ? builder->CreateICmpSGE(left, right) : builder->CreateICmpUGE(left, right);
    case NodeAnd:        return builder->CreateAnd(left, right);
    case NodeOr:         return builder->CreateOr(left, right);
    case NodeXor:        return builder->CreateXor(left, right);
    case NodeShiftLeft:  return builder->CreateShl(left, right);
    case NodeShiftRight: return builder->CreateAShr(left, right);
    case NodeShiftRightLogical: return builder->CreateLShr(left, right);
    default:
        return nullptr;
    }
}

llvm::Value * CodeGen::emitBinOp(Node *node) {
    LOG() << NodeName[node->id] << '\n';
    if (node->id < NodeAdd || node->id > NodeShiftRightLogical)
        return nullptr;

    BinaryOp *bin_op = (BinaryOp *)node;
    llvm::Value *left = emitExpr(bin_op->lhs);
    llvm::Value *right = emitExpr(bin_op->rhs);
    assert(left != nullptr && right != nullptr);
    llvm::Value *result = nullptr;

    auto ty = bin_op->type->id;
    bool need_cast = false;
    if (ty == TypeFloat || ty == TypeDouble) {
        switch (node->id)
        {
        case NodeAdd:        return builder->CreateFAdd(left, right);
        case NodeSub:        return builder->CreateFSub(left, right);
        case NodeMul:        return builder->CreateFMul(left, right);
        case NodeDiv:        return builder->CreateFDiv(left, right);
        case NodeRemainder:  return builder->CreateFRem(left, right);
        // case NodeModulo:     return builder->Create(left, right);
        case NodeEq:         return builder->CreateFCmpOEQ(left, right);
        case NodeNotEq:      return builder->CreateFCmpUNE(left, right);
        case NodeLess:       return builder->CreateFCmpOLT(left, right);
        case NodeLessEq:     return builder->CreateFCmpOLE(left, right);
        case NodeGreater:    return builder->CreateFCmpOGT(left, right);
        case NodeGreaterEq:  return builder->CreateFCmpOGE(left, right);
        default:
            break;
        }
    } else if (ty == TypeInt || ty == TypeByte || ty == TypeBool) {
        if (left->getType() != right->getType() && left->getType()->isIntegerTy(8)) {
            need_cast = true;
            left = builder->CreateIntCast(left, i32_ty, false);
        }
        llvm::Value *val = emitBinOpInt(node->id, left, right, ty == TypeInt);
        if (val) {
            if (node->id >= NodeEq && node->id <= NodeNotEq) return val;
            if (need_cast) return builder->CreateIntCast(val, byte_ty, false);
            else return val;
        }
    } else if (ty == TypeNull || ty == TypePtr || ty == TypeCStr) {
        auto lhs = builder->CreatePtrToInt(left, i64_ty);
        llvm::Value *rhs;
        if (right->getType()->isIntegerTy()) {
            rhs = builder->CreateIntCast(right, i64_ty, true);
            auto elem_sz = llvm::ConstantExpr::getSizeOf(left->getType()->getPointerElementType());
            rhs = builder->CreateMul(rhs, elem_sz);
        } else {
            rhs = builder->CreatePtrToInt(right, i64_ty);
        }
        llvm::Value *val = emitBinOpInt(node->id, lhs, rhs, false);
        if (val) {
            if (node->id >= NodeEq && node->id <= NodeNotEq) return val;
            return builder->CreateIntToPtr(val, left->getType());
        }
    }
    error(bin_op, err_types_for_operator_not_compatible, bin_op->tok->str());
}

llvm::Value * CodeGen::emitAll(Any *node) {
    Node *cond = node->cases;
    llvm::SmallVector<llvm::Value *> conds;
    auto after_all = createBasicBlock("after.all");
    llvm::Value *llvm_cond;
    while (cond) {
        auto if_then = createBasicBlock("if.then");
        auto if_else = createBasicBlock("if.else");

        llvm_cond = emitExpr(cond);
        builder->CreateCondBr(llvm_cond, if_then, if_else);

        setCurrentBasicBlock(if_else);
        builder->CreateStore(llvm_cond, tempVarBool);
        builder->CreateBr(after_all);

        setCurrentBasicBlock(if_then);
        cond = cond->next;
    }
    builder->CreateStore(llvm_cond, tempVarBool);
    builder->CreateBr(after_all);
    setCurrentBasicBlock(after_all);
    return CreateLoad(tempVarBool);
}

llvm::Value * CodeGen::emitAny(Any *node) {
    Node *cond = node->cases;
    llvm::SmallVector<llvm::Value *> conds;
    auto after_any = createBasicBlock("after.any");
    llvm::Value *llvm_cond;
    while (cond) {
        auto if_then = createBasicBlock("if.then");
        auto if_else = createBasicBlock("if.else");

        llvm_cond = emitExpr(cond);
        builder->CreateCondBr(llvm_cond, if_then, if_else);

        setCurrentBasicBlock(if_then);
        builder->CreateStore(llvm_cond, tempVarBool);
        builder->CreateBr(after_any);

        setCurrentBasicBlock(if_else);
        cond = cond->next;
    }
    builder->CreateStore(llvm_cond, tempVarBool);
    builder->CreateBr(after_any);
    setCurrentBasicBlock(after_any);
    return CreateLoad(tempVarBool);
}

llvm::Value * CodeGen::createCString(Node *node) {
    const char *data = ((Type *)node)->data.str;
    auto str = builder->CreateGlobalString(StringRef(data, node->tok->len), "", 0, M);
    str->setConstant(false);
    return getArrayPtr(str);
}

llvm::Constant * CodeGen::createArray(Array *node) {
    auto arr = llvm::ConstantDataArray::getRaw(node->toStringRef(), node->num, llvmType(node->type));
    auto *gvar = new llvm::GlobalVariable(*M, arr->getType(), false, llvm::GlobalValue::PrivateLinkage, arr);
    return gvar;
}

llvm::Value * CodeGen::emitCast(Cast *node) {
    auto val = emitExpr(node->expr);
    auto ty = val->getType();
    auto to_ty = llvmType(node->type);
    LOG() << node->tok->next->str() << " " << TypeName[node->type->id] << '\n';
    if (ty->isIntegerTy()) {
        bool is_signed = ty->isIntegerTy(32);
        if (to_ty->isFloatingPointTy()) return is_signed ? builder->CreateSIToFP(val, to_ty) : builder->CreateUIToFP(val, to_ty);
        if (to_ty->isIntegerTy()) return builder->CreateIntCast(val, to_ty, is_signed);
        if (to_ty->isPointerTy()) return builder->CreateIntToPtr(builder->CreateIntCast(val, i64_ty, true), to_ty);
    } else if (ty->isFloatingPointTy()) {
        if (to_ty->isFloatingPointTy()) return builder->CreateFPCast(val, to_ty);
        if (to_ty->isIntegerTy()) return builder->CreateFPToSI(val, to_ty);
    } else if (ty->isPointerTy()) {
        if (to_ty->isIntegerTy()) return builder->CreatePtrToInt(val, to_ty);
        if (to_ty->isPointerTy()) return builder->CreateBitCast(val, to_ty); 
    }
}

llvm::Value * CodeGen::emitNot(Unary *node) {
    auto val = emitExpr(node->value);
    return builder->CreateNot(val);
}

llvm::Value * CodeGen::emitSizeOf(Unary *node) {
    llvm::Type *ty;
    if (node->value->id < TypeEnd) {   // is a type name
        ty = llvmType(node->value);
    } else {
        auto val = emitExpr(node->value);
        ty = val->getType();
    }
    uint64_t sz;
    if (ty->isPointerTy() && ty->getPointerElementType()->isArrayTy())
        sz = ty->getPointerElementType()->getArrayNumElements();
    else
        sz = M->getDataLayout().getTypeAllocSize(ty).getFixedSize();
    return llvm::ConstantInt::get(i32_ty, sz);
}

llvm::Value * CodeGen::emitDeclare(TypedNode *node) {
    auto ty = node->type;
    bool is_struct = false;
    if (ty->id == TypePtr) {
        ty = ty->data.type;
        is_struct = (ty->id == TypeStruct);
    }
    auto type = llvmType(ty);
    auto linkage = node->is_signed ? GlobalValue::InternalLinkage : GlobalValue::PrivateLinkage;
    auto var = new llvm::GlobalVariable(*M, type, false, linkage, nullptr, node->tok->str());
    if (is_struct) var->setInitializer(llvm::ConstantAggregateZero::get(type));
    else var->setInitializer(llvm::Constant::getNullValue(type));
    return var;
}

llvm::Value * CodeGen::emitExpr(Node *node) {
    LOG() << NodeName[node->id] << '\n';
    if (node->tok) LOG() << node->tok->str() << '\n';

    llvm::Value *val = nullptr;
    switch (node->id)
    {
    case NodeEither:       val = emitEither((Either *)node); break;
    case NodeSwitch:       val = emitSwitch((Switch *)node); break;
    case NodeAny:          val = emitAny((Any *)node); break;
    case NodeAll:          val = emitAll((Any *)node); break;
    case NodeCase:         val = emitCase((Case *)node); break;
    case NodeCast:         val = emitCast((Cast *)node); break;
    case NodeFnCall:       val = emitCall((FnCall *)node); break;
    case NodeVar:          val = readVariable((Variable *)node); break;
    case NodeIdent:        val = readVariable(((Identifier *)node)->var); break;
    case NodePtr:          val = getPointer(((Pointer *)node)->value); break;
    case TypeInt:          val = getConstantInt(node); break;
    case TypeByte:         val = getConstantInt(node); break;
    case TypeFloat:
    case TypeDouble:       val = getConstantFloat(node); break;
    case TypeCStr:         val = createCString(node); break;
    case TypeBool:         {
        Type *ty = (Type *)node;
        val = llvm::ConstantInt::getBool(Ctx, ty->data.value);
    } break;
    case NodeArray:        val = createArray((Array*)node); break;
    case NodeMemberAccess: val = readStructMember((Path *)node); break;
    case NodePtrIdx:       val = readPointerValue((Path *)node); break;
    case NodeSizeOf:       val = emitSizeOf((Unary *)node); break;
    case NodeNot:          val = emitNot((Unary *)node); break;
    case NodeDeclare:      val = emitDeclare((TypedNode *)node); break;
    case NodeNull:         val = llvm::ConstantPointerNull::get(byte_ty->getPointerTo()); break;
    default:               val = emitBinOp(node); break;
    }
    curVal = val;
    return val;
}

llvm::Value * CodeGen::emitAssignment(Assignment *node) {
    auto dest_id = node->lhs->id;
    llvm::Value *dest;
    Variable *left;
    if (dest_id == NodeVar) {
        left = (Variable *)node->lhs;
        dest = left->ir;
    } else if (dest_id == NodeMemberAccess || dest_id == NodePtrIdx) {   // set-path
        Path *path = (Path *)node->lhs;
        dest = getElementPtr(path);
    }

    llvm::Value *val = nullptr;
    if (node->rhs->id == NodeAssign) {
        val = emitAssignment((Assignment *)node->rhs);
    } else {
        val = emitExpr(node->rhs);
    }
    assert(val != nullptr);
LOG() << node->lhs->tok->str() << '\n';
    if (dest_id == NodeVar && left->hasAttr(VarGlobal)) {
        auto ty = left->type;
        if (ty->id == TypePtr) {
            left->ir = val;
            return nullptr;
        } else {
            if (dest == nullptr) {
                if (ty->id == TypePtr) ty = ty->data.type;
                auto var = emitGlobalVar(left, ty);
                if (llvm::isa<llvm::Constant>(val)) {
                    var->setInitializer(llvm::cast<llvm::Constant>(val));
                    return val;
                } else {
                    var->setInitializer(llvm::Constant::getNullValue(llvmType(ty)));
                    dest = var;
                }
            }
        }
    }

    if (val->getType()->isPointerTy()) {
        val = builder->CreateBitCast(val, dest->getType()->getPointerElementType());
    }
    builder->CreateStore(val, dest);
    return val;
}

void CodeGen::emitThrow(Throw *node) {
    auto val = emitExpr(node->value);
    auto system = M->getNamedGlobal("system");
    auto thrown = builder->CreateStructGEP(system->getType()->getScalarType()->getPointerElementType(), system, 9);
    builder->CreateStore(val, thrown);

    auto arg_throw = curFunc->getArg(0);
    builder->CreateStore(val, arg_throw);
    if (returnVal) builder->CreateRet(CreateLoad(returnVal));
    else builder->CreateRetVoid();
}

void CodeGen::emitCatch(Catch *node) {
    in_catch = true;
    catchFilter = emitExpr(node->filter);
    afterCatchBB = createBasicBlock("after.catch");
    emitCode(node->body);
    builder->CreateBr(afterCatchBB);
    setCurrentBasicBlock(afterCatchBB);
    auto exception_val = curFunc->getArg(0);
    builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), exception_val);
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
        case NodeBreak:  emitBreak(node); return nullptr;
        case NodeContinue: emitContinue(node); return nullptr;
        case NodeReturn: builder->CreateRet(emitExpr(((Return *)node)->expr)); return nullptr;
        case NodeExit: builder->CreateRetVoid(); return nullptr;
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

llvm::FunctionType * CodeGen::createFuncType(FuncType *fn) {
    LOG() << fn->tok->str() << '\n';
    auto result_ty = llvm::Type::getVoidTy(Ctx);
    if (fn->return_type->id != TypeVoid)
        result_ty = llvmType(fn->return_type);

    llvm::SmallVector<llvm::Type *> params_ty;
    auto params = fn->params;
    while (params) {
        params_ty.push_back(llvmType(params));
        params = params->next;
    }

    fn->ir = llvm::FunctionType::get(result_ty, params_ty, fn->hasAttr(FuncVariadic));
    return fn->ir;
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
    bool has_created = false;
    llvm::Function *func;
    std::string name;
    if (fn->import_lib) {
        name = fn->import_name->str().str();
        auto it = importNames.find(name);
        if (it != importNames.end()) {
            has_created = true;
            auto f = it->second;
            fn->type->ir = f->type->ir;
            fn->ir = f->ir;
        }
    } else {
        mangleName(fn->import_name, ctx, name);
    }

    if (!has_created) {
        auto func_ty = createFuncType(fn->type);
        auto linkage = fn->isExtern() ? GlobalValue::ExternalLinkage : GlobalValue::InternalLinkage;
        func = llvm::Function::Create(func_ty, linkage, name, *M);
        // process attributes
        func->setCallingConv(getCallingConv(fn->type->attrs));
        fn->ir = func;

        if (fn->import_lib) importNames.insert({name, fn});
    }

    if (auto lib = fn->import_lib) import_libs.insert(lib->str());
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
    if (fn->import_lib) return;

    llvm::Function *func = llvm::cast<llvm::Function>(fn->ir);
    curFunc = func;
    auto entry_block = BasicBlock::Create(Ctx, "entry", func);
    setCurrentBasicBlock(entry_block);

    auto alloca_on_stack = [this](Node *node) {
        Node head{};
        Node *cur = &head;
        while (node) {
            Variable *var = (Variable *) node;
            if (var->type) {
                var->ir = builder->CreateAlloca(llvmType(var->type));
            } else {
                error(var, warn_unused_var);
            }
            cur = cur->next = var;
            node = node->next;
        }
    };

    // allocate args on stack
    alloca_on_stack(fn->type->params);
    Variable *var = (Variable *)fn->type->params;
    for (auto &arg : func->args()) {
        builder->CreateStore(&arg, var->ir);
        var = (Variable *)var->next;
    }
    // allocate local variables on stack
    alloca_on_stack(fn->locals);
    if (fn->hasAttr(FuncHasAnyAll)) tempVarBool = builder->CreateAlloca(bool_ty);
    if (fn->hasAttr(FuncHasLoop)) loopCounter = builder->CreateAlloca(i32_ty);
    auto ret_ty = fn->type->return_type;
    if (ret_ty->id != TypeVoid) returnVal = builder->CreateAlloca(llvmType(ret_ty));
    else returnVal = nullptr;

    if (fn->n_typed_value > 0) {
        auto typed_value = llvm::StructType::create(Ctx, {i32_ty, i64_ty}, "", true);
        auto arr = llvm::ArrayType::get(typed_value, fn->n_typed_value);
        typedValues = builder->CreateAlloca(arr);
    }

    emitCode(fn->body.node);
    if (!curBB->getTerminator()) {
        if (ret_ty->id != TypeVoid) {
            if (llvm::isa<llvm::ConstantPointerNull>(curVal))
                curVal = builder->CreateBitCast(curVal, llvmType(ret_ty));
            builder->CreateRet(curVal);
        } else {
            builder->CreateRetVoid();
        }
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
    auto func_ty = createFuncType(fn->type);
    auto llvm_fn = llvm::Function::Create(func_ty, GlobalValue::InternalLinkage, "__Red/System__MainStartup", *M);
    llvm_fn->addFnAttr(llvm::Attribute::NoBuiltin);
    llvm_fn->addFnAttr(llvm::Attribute::NoInline);
    llvm_fn->addFnAttr(llvm::Attribute::NoReturn);
    llvm_fn->addFnAttr(llvm::Attribute::get(Ctx, llvm::Attribute::StackAlignment, 16));

    fn->ir = llvm_fn;
    emitFunc(fn);
}

void CodeGen::createEntryFunc(Function *main_fn) {
    auto func_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
    auto llvm_fn = llvm::Function::Create(func_ty, GlobalValue::ExternalLinkage, comp.entryFuncName, *M);
    curFunc = llvm_fn;

    llvm_fn->addFnAttr(llvm::Attribute::NoInline);
    llvm_fn->addFnAttr(llvm::Attribute::NoReturn);
    llvm_fn->addFnAttr(llvm::Attribute::get(Ctx, llvm::Attribute::StackAlignment, 16));

    auto entry_block = BasicBlock::Create(Ctx, "entry", llvm_fn);
    setCurrentBasicBlock(entry_block);

    if (main_fn->isThrowable()) {
        auto thrown = builder->CreateAlloca(i32_ty);
        auto zero = llvm::ConstantInt::get(i32_ty, 0);
        builder->CreateStore(zero, thrown);
        builder->CreateCall(main_fn->type->ir, main_fn->ir, {thrown});

        llvm::Value *exception = CreateLoad(thrown);
        auto no_exception_bb = createBasicBlock("no_exception");
        auto catch_bb = createBasicBlock("catch");
        builder->CreateCondBr(builder->CreateICmpEQ(exception, zero), no_exception_bb, catch_bb);

        // handle exception
        setCurrentBasicBlock(catch_bb);
        auto handler = M->getFunction("***-uncaught-exception");
        if (handler) builder->CreateCall(handler);
        builder->CreateRetVoid();

        setCurrentBasicBlock(no_exception_bb);
    } else {
        builder->CreateCall(main_fn->type->ir, main_fn->ir);
    }
    builder->CreateRetVoid();
}

void CodeGen::output() {
    using namespace llvm;
    using OptimizationLevel = typename llvm::OptimizationLevel;

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

// void CodeGen::emitGlobals(Context *ctx) {
//     for (auto const &val : ctx->decls) {
//         Variable *var = (Variable *)val.second;
//         if (var->id == NodeVar) {
//             emitGlobalVar(var, var->type);
//         } else if (var->id == NodeContext) {
//             emitGlobals((Context *)var);
//         }
//     }
// }

void CodeGen::emit(Context *ctx) {
    // for Windows only
    //new llvm::GlobalVariable(*M, i32_ty, false, GlobalValue::ExternalLinkage, llvm::ConstantInt::get(i32_ty, 1), "_fltused");
    createFunctions(ctx);
    emitMainFunc(ctx->func);
    emitFunctions(ctx);
    createEntryFunc(ctx->func);
    output();
}

llvm::StructType * CodeGen::createStruct(Struct *st) {
    LOG() << st->tok->str() << '\n';
    if (st->ir) return st->ir;

    auto st_ty = llvm::StructType::create(Ctx, st->tok->str());
    st->ir = st_ty;
    llvm::SmallVector<llvm::Type *> types;
    auto member = st->data.member;
    while (member) {
        types.push_back(llvmType(((Member *)member)->type));
        member = member->next;
    }
    st_ty->setBody(types, st->is_signed);
    return st_ty;
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
    case TypeNull:
    case TypeCStr:   return byte_ty->getPointerTo();
    case TypeStruct: return createStruct((Struct *)ty);
    //case TypeArray:  return llvm::ArrayType::get(byte_ty, ty->tok->len);
    case TypeFuncTy: {
        auto ftype = (FuncType *)ty;
        llvm::Type *t = ftype->ir ? ftype->ir : createFuncType(ftype);
        return t->getPointerTo();
    }
    case TypeVoid:
            error(node, err_not_initialized, node->tok->str());
    default:
        break;
    }
    llvm_unreachable("");
    return nullptr;
}

template <typename... Args>
void CodeGen::error(const Node *node, unsigned diag_id, Args &&... args) {
    comp.error(node->tok, diag_id, std::forward<Args>(args)...);
}
