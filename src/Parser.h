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
    TypeNull,
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
    bool is_alias;
    bool is_static;     // declare
    bool is_extern;
    bool is_signed;
    uint16_t align;
    Node *next;
    Token *tok;         // main token
};

typedef NodeId TypeId;

struct Type : Node {
    uint32_t  size;
    int32_t   value;
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
    std::vector<Node *> functions;
    std::vector<Node *> data;       // global variables
    Node *entry;                    // entry function
    Context *ctx;                   // global context
};

class Compilation;

class Parser : protected Utils
{
private:
    typedef Buffer<unsigned char, 1024 * 1024> NodeBuffer; 

    Program *program;
    Function *curFunc;
    Context  *curCtx;

    NodeBuffer nodeBuf;

public:
    Parser(Compilation &c) : Utils(c) {program = newNode<Program>(NodeProgram, nullptr);};
    Parser(Compilation &&) = delete;  // prevents rvalue binding

    Program *parse(Token *tok);

private:
    void addDecl(Context *ctx, StringRef&& name, Node *node);
    Node * findDecl(Context *ctx, StringRef name);
    const Type * findType(StringRef&& name);
    const Type * parseType(Token *tok, Token **rest);

    Token * parseLocal(Token *tok, Function *func);
    Token * parseFuncSpec(Token *tok, Function *func);
    Token * parseEnum(Token *tok, Context *ctx);
    Node * parseDecl(Token *tok, Token **rest);
    Token * collectDeclarations(Token *tok, Context *ctx, bool is_block = false);

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
