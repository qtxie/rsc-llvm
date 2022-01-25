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
#include <llvm/ADT/BitmaskEnum.h>

using llvm::StringMap;

namespace llvm {
    class Value;
}

typedef llvm::Value IrValue;

enum FuncAttr {
    FuncStdcall  = 0x0001,
    FuncCdecl    = 0x0002,
    FuncCallConv = 0x0003,
    FuncInfix    = 0x0004,
    FuncCallback = 0x0008,
    FuncVariadic = 0x0010,
    FuncTyped    = 0x0020,
    FuncCustom   = 0x0040,
    FuncCatch    = 0x0080,
    FuncInline   = 0x0100,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ FuncInline)
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
};

typedef StringMap<Node *> HashMap;
typedef NodeId TypeId;

struct Type : Node {
    uint32_t  tid;     // unique id
    union {
        double fvalue;
        int64_t value;
        const Type *type;
        Node *member;
        Token *st_body; // struct body
        Token *import_lib;
    } data;
};

// Struct member
struct Member : Type {
    int idx;
    int align;
    int offset;

    // Bitfield
    // bool is_bitfield;
    // int bit_offset;
    // int bit_width;
};

struct Function : Type {
    Token *import_name;
    uint32_t n_params;
    uint32_t n_locals;
    Node *params;
    Node *locals;
    const Type *return_type;
    Token *spec;
    union {
        Token *token;
        Node  *node;
    } body;
    union {
        Token *token;
        FuncAttr value;
    } attrs;
    IrValue *ir;
};

struct Variable : Node {
    Type *type;
    Node *init_data;
    VarAttr attrs;
    int offset;     // for local variable
    IrValue *ir;
};

struct Identifier: Node {
    Variable *var;
};

struct BinaryOp : Node {
    Node *lhs;
    Node *rhs;
};

typedef BinaryOp Assignment;

struct Unary : Node {
    Node *value;
};

struct MemberAccess : Node {
    Node *member;
};

struct FnCall : Node {
    Function *fn;
    Node *args;
};

struct Return : Node {
    Node *expr;
};

struct If : Node {
    Node *cond;
    Node *body;
};

struct Either : Node {
    Node *cond;
    Node *then_body;
    Node *els_body;
};

struct Context : Node {
    Context *parent;
    HashMap decls;             // for struct, enum, function, alias
    HashMap vars;              // for variables
};

struct Program : Node {
    Node *functions;
    Node *data;                // global variables
    Node *ast;
    Context *ctx;              // global context
};

class Compilation;
class CodeGen;

class Parser : protected Utils
{
private:
    CodeGen &codegen;

    typedef Buffer<unsigned char, 1024 * 1024> NodeBuffer; 

    uint32_t curTypeId;
    Node    *curFunc;
    Node    *curGlobal;
    Context *curCtx;
    SmallVector<uint32_t> tids;  // detect struct circular reference
    SmallVector<Token *> import_libs;

    NodeBuffer nodeBuf;

    Logger LOG;

public:
    Parser(Compilation &c, CodeGen &_codegen) : Utils(c), codegen(_codegen), curTypeId(1000) {};
    Parser(Compilation &&) = delete;  // prevents rvalue binding

    Program *parse(Token *tok);

private:
    Variable * addVar(Context *ctx, Token * name, bool is_local = false);
    void addDecl(Context *ctx, Token * name, Node *node);
    Context * findContext(Token *tok);
    Variable * findVar(Token * name, Context **ctx_out);
    Node * findDecl(Token * name, Context **ctx_out);
    const Type * findType(Token * name,  Context **ctx_out);

    Unary *getStructMember(Unary *st, Token *tok);

    Node * checkArgType(Node *arg, Type *expected);

    const Type * parseType(Token *tok, Token **rest, bool is_block = true, bool is_collect = false);
    Node * parseIf(Token *tok, Token **rest);
    Node * parseReturn(Token *tok, Token **rest);
    Node * parseFnCall(Function *fn, Token *tok, Token **rest);
    Node * parseAssignment(Token *tok, Token **rest);
    Node * parseExpr(Token *tok, Token **rest);
    Token * parseLocal(Token *tok, Function *func);
    Token * parseFuncSpec(Token *tok, Function *func, bool is_collect = false);
    Token * parseEnum(Token *tok, Context *ctx);
    Token * parseImport(Token *tok, Context *ctx);
    Node * parseMembers(Token *tok, Token **rest);
    Type * parseStruct(Token *tok, Token **rest, bool is_collect = false);
    Node * parsePath(Token *tok, Token **rest, bool &is_setpath);
    Node * parseBinaryOp(Node *lhs, Token *tok, Token **rest);
    Node * parseCode(Token *tok, Token **rest, Context *ctx, bool is_block = true);
    Node * parseCodeBlock(Token *tok, Context *ctx, bool is_func_body = false);
    void parseFunctions(Context *ctx);

    const Type * collectType(Token *tok, Token **rest) {
        return parseType(tok, rest, false, true);
    }
    Node * collectDecl(Token *tok, Token **rest);
    Token * collectDeclarations(Token *tok, Context *ctx, bool is_block = true, bool is_func_ctx = false);

    bool resolveStruct(Type *ty, Context *ctx);
    void resolveDeclarations(Context *ctx);

    Variable *newVar(Token *tok, bool is_global = false);

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
};

// For debugging
struct DumpState {
    bool is_bin_op;
    uint32_t indent;
    uint32_t nNode;
};
void dumpFunction(Function *func);
void dumpAst(Node *node, DumpState s = {});

#endif /* RED_SYSTEM_PARSER_H */
