/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_PARSER_H
#define RED_SYSTEM_PARSER_H

#include "LLVM.h"
#include "Token.h"
#include "StringMap.h"

using llvm::StringMap;

enum TypeId : uint8_t {
    TypeInvalid,
    TypeVoid,
    TypeBool,
    TypeByte,
    TypeInt,
    TypeFloat,
    TypeFloat64,
    TypeEnum,
    TypeBinary,
    TypePtr,
    TypeFunc,
    TypeArray,
    TypeStruct,
    TypeNull,
};

struct Type {
    TypeId  id;
    int     size;
    int     align;
    bool    is_unsigned;
};

// AST node type
enum NodeType : uint8_t {
    NodeError,

    NodeIf,
    NodeEither,
    NodeSwitch,
    NodeCase,
    NodeWhile,
    NodeUntil,
    NodeAssign
};

// AST Node
struct Node {
    NodeType type;
    bool is_alias;
    bool is_static;     // declare
    bool is_extern;
    Node *next;
};

struct AssignNode : Node {

};

struct IfNode : Node {

};

struct EitherNode : Node {

};

struct Function : Node {
    StringRef name;
    Node *params;
    Node *locals;

    Type *return_type;
    Node *body;

    StringRef import_lib;

    bool is_inline;
};

struct Context : Node {
    Context *parent;
    StringMap<Node *> decls;     // global declarations in this context
};

struct Program {
    std::vector<Node *> functions;
    std::vector<Node *> data;
    Node *entry;                 // entry function
    Context *ctx;                // global context
};

class Compilation;

class Parser
{
private:
    typedef Buffer<unsigned char, 1024 * 1024> NodeBuffer; 

    Compilation &comp;

    Program *program;
    Function *curFunc;

    NodeBuffer nodeBuf;

public:
    Parser(Compilation &c) : comp(c) {program = newNode<Program>();};
    Parser(Compilation &&) = delete;  // prevents rvalue binding

    Program *parse(const Token *tok);

private:
    void collectDeclarations(const Token *tok, Context *ctx);

    template <typename T>
    T * newNode() {
        return new (nodeBuf.alloc(sizeof(T))) T;
    }
};

#endif /* RED_SYSTEM_PARSER_H */
