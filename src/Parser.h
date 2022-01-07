/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_PARSER_H
#define RED_SYSTEM_PARSER_H

#include "Utils.h"
#include "StringMap.h"

using llvm::StringMap;

// AST node type
enum NodeId : uint8_t {
    NodeUndef,
    TypeVoid,
    TypeNull,
    TypeBool,
    TypeByte,
    TypeInt,
    TypeFloat,
    TypeDouble,
    TypeEnum,
    TypeBinary,
    TypePtr,
    TypeFunc,
    TypeArray,
    TypeStruct,
    TypeAlias,
    NodeAssign,
    NodeFunc,
    NodeFuncCall,
    NodeVar,
    NodeNull,
    NodeByte,
    NodeBool,
    NodeInt,
    NodeFloat,
    NodeString,
    NodeArray,
    NodeStruct,
    NodeContext,
    NodePointer,
    NodeEnum,
    NodeGetAddr,        // :abc
    NodeOpAdd,          // +
    NodeOpSub,          // -
    NodeOpMul,          // *
    NodeOpDiv,          // /
    NodeOpAnd,          // and
    NodeOpOr,           // or
    NodeOpXor,          // xor
    NodeOpEq,           // =
    NodeOpNotEq,        // <>
    NodeOpLess,         // <
    NodeOpGreater,      // >
    NodeOpLessEq,       // <=
    NodeOpGreaterEq,    // >=
    NodeNot,
    NodeAny,
    NodeAll,
    NodeReturn,
    NodeIf,
    NodeEither,
    NodeSwitch,
    NodeCase,
    NodeWhile,
    NodeUntil,
    NodeProgram
};

// AST Node
struct Node {
    NodeId id;
    bool is_signed;
    bool is_extern;
    uint16_t align;
    uint32_t size;
    Node *next;
    Token *tok;         // main token
};

typedef NodeId TypeId;

struct Type : Node {
    uint32_t  tid;     // unique id
    union {
        int64_t value;
        Type *type;
        Node *member;
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

struct VarNode: Node {
    Type *type;
};

struct IntegerNode : Node {

};

struct AssignNode : Node {

};

struct IfNode : Node {

};

struct EitherNode : Node {

};

struct Context : Node {
    Context *parent;
    StringMap<Node *> decls;     // for struct, enum, function, alias
    StringMap<Node *> vars;      // for variables
};

struct Function : Node {
    uint32_t n_params;
    uint32_t n_locals;
    Node *params;
    Node *locals;

    const Type *return_type;
    uint32_t n_attrs;
    uint32_t n_body;
    Token *attrs;
    Token *body;

    StringRef import_lib;

    bool is_inline;
};

struct Program : Node {
    Node *functions;
    Node *data;                // global variables
    Node *entry;               // entry function
    Context *ctx;              // global context
};

class Compilation;

class Parser : protected Utils
{
private:
    typedef Buffer<unsigned char, 1024 * 1024> NodeBuffer; 

    uint32_t curTypeId;
    Node    *curFunc;
    Node    *curGlobal;
    Context *curCtx;

    NodeBuffer nodeBuf;

public:
    Parser(Compilation &c) : Utils(c), curTypeId(1000) {};
    Parser(Compilation &&) = delete;  // prevents rvalue binding

    Program *parse(Token *tok);

private:
    void addDecl(Context *ctx, StringRef&& name, Node *node);
    Node * findDecl(Context *ctx, StringRef name);
    const Type * findType(const StringRef&& name);
    const Type * parseType(Token *tok, Token **rest, bool is_block = true);

    Token * parseLocal(Token *tok, Function *func);
    Token * parseFuncSpec(Token *tok, Function *func);
    Token * parseEnum(Token *tok, Context *ctx);
    Type * parseStruct(Token *tok, Token **rest);
    Node * parseDecl(Token *tok, Token **rest);
    Token * collectDeclarations(Token *tok, Context *ctx, bool is_block = false);

    template <typename T>
    T * newNode() {return (T *)nodeBuf.alloc(sizeof(T));};

    template <typename T>
    T * newNode(NodeId id, Token *main_tok) {
        T *node = (T *)nodeBuf.alloc(sizeof(T));
        node->id = id;
        node->tok = main_tok;
        return node;
    }

    template <typename T>
    T * newNode(const T *node) {return new (nodeBuf.alloc(sizeof(T))) T{*node};}
};

#endif /* RED_SYSTEM_PARSER_H */
