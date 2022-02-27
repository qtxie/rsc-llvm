/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_PARSER_H
#define RED_SYSTEM_PARSER_H

#include "Logger.h"
#include "Utils.h"
#include "StringMap.h"
#include <set>
#include <llvm/ADT/BitmaskEnum.h>
#include <llvm/IR/DerivedTypes.h>

using llvm::StringMap;

namespace llvm {
    class Value;
}

typedef llvm::Value IrValue;

enum FuncAttr {
    FuncStdcall      = 0x0001,
    FuncCdecl        = 0x0002,
    FuncCallConv     = 0x0003,
    FuncInfix        = 0x0004,
    FuncCallback     = 0x0008,
    FuncVariadic     = 0x0010,
    FuncTyped        = 0x0020,
    FuncCustom       = 0x0040,
    FuncCatch        = 0x0080,
    FuncInline       = 0x0100,
    FuncExtern       = 0x0200,
    FuncThrow        = 0x0400,
    FuncHasAnyAll    = 0x0800,
    FuncHasLoop      = 0x1000,

    FuncAttrEnd,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ FuncAttrEnd)
};

enum VarAttr {
    VarStatic    = 0x0001,
    VarGlobal    = 0x0002,
    VarLocal     = 0x0004,
    VarExtern    = 0x0008,
    VarTLS       = 0x0010,
};

// AST node type
enum NodeId : uint8_t {
#include "NodeId.def"
};

// AST Node
struct Node {
    NodeId id;
    bool is_signed;
    uint16_t align;
    uint32_t size;
    Node *next;
    Token *tok;         // main token

    constexpr StringRef str() {return StringRef(tok->loc, tok->len);}
    bool isa(NodeId node_id) {return id == node_id;}
};

typedef StringMap<Node *> HashMap;
typedef NodeId TypeId;

struct Type : Node {
    uint32_t  tid;     // unique id
    union {
        char chr;
        double fvalue;
        int64_t value;
        const Type *type;
        Node *member;
        Token *st_body; // struct body
        const char *str;
    } data;
};

struct TypedNode : Node {
    const Type *type;
};

struct Struct : Type {
    llvm::StructType *ir;
};

// Struct member
struct Member : TypedNode {
    int idx;
    // int align;
    // int offset;

    // Bitfield
    // bool is_bitfield;
    // int bit_offset;
    // int bit_width;
};

struct FnCall;

struct FuncType : Type {
    uint32_t n_params;
    Node *params;
    const Type *return_type;
    FuncAttr attrs;
    Token *spec;
    llvm::FunctionType *ir;

    bool isThrowable() {return static_cast<bool>(attrs & FuncThrow);}
    void addAttr(FuncAttr attr) {
        using namespace llvm;
        attrs |= attr;
    }

    bool operator <(const FuncType& rhs) const
    {
        return (n_params < rhs.n_params && attrs < rhs.attrs);
    }
};

struct Function : Node {
    FuncType *type;    //@@ make sure type is the first member
    Token *import_name;
    Token *import_lib;
    Node *locals;
    Node *temp_locals;

    union {
        Token *token;
        Node  *node;
    } body;
    std::vector<FnCall *> *calls;
    IrValue *ir;

    bool isThrowable() {return static_cast<bool>(type->attrs & FuncThrow);}
    bool isExtern() {return static_cast<bool>(type->attrs & FuncExtern);}

    void addAttr(FuncAttr attr) {
        using namespace llvm;
        type->attrs |= attr;
    }

    bool hasAttr(FuncAttr attr) {return static_cast<bool>(type->attrs & attr);}
};

struct Variable : TypedNode {
    VarAttr attrs;
    IrValue *ir;
    bool isType(const Type *ty) {return type->id == ty->id;}
    bool isType(NodeId id) {return type->id == id;}
    bool isGlobal() {return static_cast<bool>(attrs & VarGlobal);}
    void addAttr(VarAttr attr) {
        attrs = static_cast<VarAttr>(attrs | attr);
    }
};

struct Identifier: Node {
    Variable *var;
    bool isType(NodeId type_id) {return var->type->id == type_id;}
};

struct BinaryOp : TypedNode {
    Node *lhs;
    Node *rhs;
};

typedef BinaryOp Assignment;

struct Unary : Node {
    Node *value;
};

struct Path : Node {
    Node *first;
    Node *value;
};

struct Pointer : TypedNode {
    Node *value;
    bool isType(NodeId type_id) {
        return ((Variable *)value)->type->id == type_id;
    }
};

struct FnCall : Node {
    FuncType *type;  //@@ make sure type is the first member
    Function *fn;
    Node *args;
};

struct Return : Node {
    Node *expr;
};

struct Throw : Node {
    Node *value;
};

struct Catch : Node {
    Node *filter;
    Node *body;
    Node *handler;
};

struct Cast : TypedNode {
    Node *expr;
};

typedef std::vector<std::pair<Node *, Node *>> SwitchCases;

struct Switch : Node {
    Node *value;
    SwitchCases *cases;
    Node *defaultCase;
};

struct If : Node {
    Node *cond;
    Node *body;
};

struct Loop : Node {
    Node *counter;
    Node *body;
};

struct While : Node {
    Node *cond;
    Node *body;
};

struct Until : Node {
    Node *body;
};

struct Either : Node {
    Node *cond;
    Node *then_body;
    Node *else_body;
};

struct Case : Node {
    Either *cases;
};

struct Any : Node {
    Node *cases;
};

struct Context : Node {
    HashMap decls;             // for struct, enum, function, alias, context, variable
    Function *func; 
    Context *parent;
    bool is_root;
};

class Compilation;
class CodeGen;

class Parser : protected Utils
{
private:
    CodeGen &codegen;

    typedef Buffer<unsigned char, 1024 * 1024> NodeBuffer; 

    uint32_t curTypeId;
    Context *curCtx;
    SmallVector<uint32_t> tids;  // detect struct circular reference
    SmallVector<Token *> import_libs;

    bool isGlobal;
    bool isDeclare;
    std::set<FuncType> funcTypeCache;
    SmallVector<std::tuple<Function*, FnCall*, bool>> unresolvedCalls;
    Token hiddenParamTok;
    Type uintPtrTy;
    Type uintTy;
    Type voidTy;
    Type nullTy;
    Type boolTy;
    NodeBuffer nodeBuf;

    Logger LOG;

public:
    Parser(Compilation &c, CodeGen &_codegen) : Utils(c), codegen(_codegen), curTypeId(1000) {
        isGlobal = false;
        isDeclare = false;
        hiddenParamTok.id = TokenIdWord;
        hiddenParamTok.loc = "__r/s_throw_";
        hiddenParamTok.len = std::char_traits<char>::length(hiddenParamTok.loc);
        uintTy = Type{TypeInt, false, 4, 4, nullptr, nullptr, TypeInt};
        boolTy = Type{TypeBool, false, 1, 1, nullptr, nullptr, TypeBool};
        voidTy = Type{TypeVoid};
        nullTy = Type{TypeNull};
        uintPtrTy.id = TypePtr;
        uintPtrTy.data.type = &uintTy;
    };

    Parser(Compilation &&) = delete;  // prevents rvalue binding

    Context *parse(Token *tok);

private:
    Variable * addVar(Context *ctx, Token * name, Variable *var = nullptr, bool is_local = false);
    void addDecl(Context *ctx, Token * name, Node *node);
    Context * findContext(Token *tok);
    Variable * findVar(Token * name, Context **ctx_out);
    Node * findDecl(Token * name, Context **ctx_out);
    const Type * findType(Token * name,  Context **ctx_out);

    Unary *getStructMember(const Type *st, Token *tok);

    Node * checkArgType(Node *arg, Type *expected);
    const Type * addVarType(Node *node);
    const Type * analyzeMathType(BinaryOp *bin);
    const Type * analyzeNodeType(Node *node);
    const Type * analyzeType(Node *node);

    void addCurFuncAttr(FuncAttr attr) {
        using namespace llvm;
        curCtx->func->type->attrs |= attr;
    }

    char scanByte(const char **bp);
    const Type * parseType(Token *tok, Token **rest, bool is_block = true, bool is_collect = false, bool error = true);
    Node * parseByte(Token *tok);
    Node * parseString(Token *tok);
    Node * parseAnyAll(Token *tok, Token **rest, NodeId id);
    Node * parseAll(Token *tok, Token **rest) {return parseAnyAll(tok, rest, NodeAll);}
    Node * parseAny(Token *tok, Token **rest) {return parseAnyAll(tok, rest, NodeAny);}
    Node * parseCase(Token *tok, Token **rest);
    Node * parseSwitch(Token *tok, Token **rest);
    Node * parseLoop(Token *tok, Token **rest);
    Node * parseUntil(Token *tok, Token **rest);
    Node * parseWhile(Token *tok, Token **rest);
    Node * parseEither(Token *tok, Token **rest);
    Node * parseIf(Token *tok, Token **rest);
    Node * parseCatch(Token *tok, Token **rest);
    Node * parseThrow(Token *tok, Token **rest);
    Node * parseReturn(Token *tok, Token **rest);
    Node * parseDeclare(Token *tok, Token **rest);
    Node * parseSizeOf(Token *tok, Token **rest);
    Node * parseFnCall(FuncType *ftype, Function *fn, Token *tok, Token **rest);
    Node * parseAssignment(Token *tok, Token **rest);
    Node * parseExpr(Token *tok, Token **rest);
    Node * parseCast(Token *tok, Token **rest);
    Token * parseLocal(Token *tok, Function *func);
    Token * parseFuncSpec(Token *tok, FuncType *ftype, Function *func, bool is_collect = false);
    Token * parseEnum(Token *tok, Context *ctx);
    Token * parseImport(Token *tok, Context *ctx);
    Node * parseMembers(Token *tok, Token **rest);
    Type * parseStruct(Token *tok, Token **rest, bool is_collect = false);
    Node * parsePath(Token *tok, Token **rest, bool &is_setpath);
    Node * parseBinaryOp(Node *lhs, Token *tok, Token **rest);
    Node * parseCode(Token *tok, Token **rest, Context *ctx, bool is_block = true);
    Node * parseCodeBlock(Token *tok, Context *ctx, bool is_func_body = false);
    void parseFunctions(Context *ctx);
    void analyzeCalls(Function *fn, bool is_main_func = true);
    void updateCall(Function *fn, FnCall *call, bool is_main_func);

    const Type * collectAlias(Token *tok, Token **rest) {
        return parseType(tok, rest, false, true);
    }
    Node * collectDecl(Token *tok, Token **rest);
    Token * collectDeclarations(Token *tok, Context *ctx, bool is_block = true, bool is_func_ctx = false);

    bool resolveStruct(Type *ty, Context *ctx);
    void resolveDeclarations(Context *ctx);

    Cast *newCast(Node *val, const Type *type) {
        auto node = newNode<Cast>(NodeCast, val->tok);
        node->expr = val;
        node->type = type;
        return node;
    }

    Function *newFunction(Token *tok) {
        auto func = newNode<Function>(TypeFunc, tok);
        func->type = newNode<FuncType>(TypeFuncTy, tok);
        return func;
    }

    Context *newContext(Token *main_tok) {
        Context *c = new (nodeBuf.alloc(sizeof(Context))) Context;
        c->id = NodeContext;
        c->tok = main_tok;
        return c;
    }

    Type * newPtrType(const Type *basetype) {
        Type *ptr = (Type *)nodeBuf.alloc(sizeof(Type));
        ptr->id = TypePtr;
        ptr->tid = TypePtr;
        ptr->data.type = basetype;
        return ptr;
    }

    Pointer *newPtrNode(Node *val, Token *tok) {
        auto ptr = newNode<Pointer>(NodePtr, tok);
        ptr->type = newPtrType(((TypedNode *)val)->type);
        ptr->value = val;
        return ptr;
    }

    template <typename T>
    T * newNode() {return (T *)nodeBuf.alloc(sizeof(T));};

    template <typename T>
    T * newNode(NodeId id, Token *main_tok = nullptr) {
        T *node = (T *)nodeBuf.alloc(sizeof(T));
        node->id = id;
        node->tok = main_tok;
        return node;
    }

    template <typename T>
    T * newNode(NodeId id, Node *lhs, Node *rhs) {
        T *node = (T *)nodeBuf.alloc(sizeof(T));
        node->id = id;
        node->lhs = lhs;
        node->rhs = rhs;
        return node;
    };

    template <typename T>
    T * copyNode(const T *node) {return new (nodeBuf.alloc(sizeof(T))) T{*node};}

    template <typename... Args>
    void error(const Node * node, unsigned diag_id, Args &&... args);
};

// For debugging
struct DumpState {
    bool is_bin_op = false;
    uint32_t indent = 0;
    uint32_t nNode;
};
void dumpFunction(Function *func);
void dumpNode(Node *node, DumpState &s);
void dumpAst(Node *node, DumpState s = {});

#endif /* RED_SYSTEM_PARSER_H */
