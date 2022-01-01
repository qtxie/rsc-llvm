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

struct Program {

};

struct Type {

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
    Node *next;
};

struct IfNode : Node {

};

struct EitherNode : Node {

};

class Compilation;

class Parser
{
private:
    Compilation &ctx;

public:
    Parser(Compilation &c) : ctx(c) {};
    Parser(Compilation &&) = delete;  // prevents rvalue binding

    Program *parse(Token *tok);

};

#endif /* RED_SYSTEM_PARSER_H */
