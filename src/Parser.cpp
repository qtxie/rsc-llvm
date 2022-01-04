/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "Parser.h"
#include "Compilation.h"

/** Grammar
 * 
 *  program           = code-block
 *  code-block        = "[" (directive | declaration | statement | expression)* "]"
 *
 *  directive         = (syscall | import | enum)*
 *  syscall           = "#syscall" "[" word: integer func-spec-block "]"
 *  import            = "#import" "[" library-functions* "]"
 *  enum              = "#enum" word "[" word (word | word:+ integer)+ "]"
 *
 *  library-functions = string ("stdcall" | "cdecl") "[" (word: string spec-block)* "]"
 *  spec-block        = func-spec-block | var-spec-block
 *  func-spec-block   = "[" func-attrs-block? doc-string? param* return-spec? ("/local" local-var+)? "]"
 *  var-spec-block    = "[" type "]"
 *  func-attrs-block  = "[" "infix" | "catch" | "callback" | "custom" | "stdcall" | "cdecl" (variadic | typed)? "]"
 *  param             = typed-word doc-string?
 *  typed-word        = word "[" type "]"
 *  local-var         = word+ ("[" type "]")?
 *  return-spec       = "return:" "[" type "]"
 * 
 *  declaration       = qualified-word: (function-decl | context-decl | alias-decl)
 *  function-decl     = ("func" | "function") func-spec-block body-block
 *  context-decl      = "context" code-block
 *  alias-decl        = "alias" type
 *  qualified-word    = ((word/)*)word
 *  path              = path-head/(word | integer)
 *  path-head         = word | path
 *  body-block        = "[" (directive | alias-decl | statement | expression)* "]"
 * 
 *  statement         = conditional-stmt | return-stmt | with-stmt | use-stmt | assignment
 * 
 *  conditional-stmt  = if-stmt | either-stmt | case-stmt | switch-stmt | while-stmt | until-stmt
 *  if-stmt           = "if" cond-expr body-block
 *  either-stmt       = "either" cond-expr body-block body-block
 *  case-stmt         = "case" "[" (cond-expr body-block)+ "]"
 *  switch-stmt       = "switch" expression "[" (selectors body-block)* ("default" body-block)? "]"
 *  while-stmt        = "while" cond-block body-block
 *  until-stmt        = "until" cond-block 
 *  return-stmt       = "exit" | "return" expression
 *  with-stmt         = "with" (qualified-word | "[" qualified-word+ "]") body-block
 *  use-stmt          = "use" "[" param+ "]" body-block
 *  cond-expr         = expression
 *  selectors         = (integer-literal | byte-literal)+
 * 
 *  assignment        = lh-side rh-expression
 *  lh-side           = qualified-word: | path:
 *  rh-expression     = expression | "declare" type 
 * 
 *  expression        = call | value | type-cast | size-enquiry | short-circuit | binary-operation
 *  call              = call-fixed-arg | call-variadic
 *  call-fixed-arg    = qualified-word expression*
 *  call-variadic     = qualified-word (expression | "[" expression+ "]")
 * 
 *  value             = literal | "(" expression ")" | word | path | get-word | get-path
 * 
 *  type-cast         = "as" (type | "[" type "]") "keep"? expression
 *  type              = integer! | byte! |  logic! | c-string! | float! | float32! | float64! | subroutine! |
 *                      func-type | pointer-type | struct-type | alias-name | enum-type-name
 *  func-type         = function! func-spec-block
 *  pointer-type      = pointer! "[" type "]"
 *  struct-type       = struct! "[" typed-word "]"
 * 
 *  size-enquiry      = "size?" (type | string-literal)
 * 
 *  short-circuit     = all | any | either | if | switch | case
 *  all               = "all" "[" expression+ "]"
 *  any               = "any" "[" expression+ "]"
 * 
 *  binary-operation  = value infix-op expression
 *  infix-op          = math-op | bitwise-op | comparison-op | qualified-word
 *  math-op           = "+" | "-" | "*" | "/" | "//" | "%"
 *  bitwise-op        = "and" | "or" | "xor" | "<<" | ">>" | ">>>"
 *  comparison-op     = "=" | "<>" | "<" | "<=" | ">=" | ">"
 *
 **/

void Parser::addNode(Context *ctx, StringRef&& name, Node *node) {
    auto& decls = ctx->decls;
    auto&& [keyval, is_new] = decls.try_emplace(name, node);
    if (!is_new)
        comp.error(name.data(), err_symbol_declared, name);
}

Node * Parser::findDecl(Context *ctx, StringRef name) {
    auto& decls = ctx->decls;
    auto it = decls.find(name);
    if (it != decls.end())
        comp.error(name.data(), err_symbol_declared, name);
    return it->second;
}

constexpr auto genDatatypes()
{
    std::array<std::pair<StringRef, Type>, 6> datatypes = {{
        {"byte!", Type{TypeByte, false, false, false, true, 1, nullptr, nullptr, 1}},
        {"c-string!", Type{TypePtr, false, false, false, true, 4, nullptr, nullptr, 4}},
        {"float!", Type{TypeDouble, false, false, false, true, 8, nullptr, nullptr, 8}},
        {"float32!", Type{TypeFloat, false, false, false, true, 4, nullptr, nullptr, 4}},
        {"integer!", Type{TypeInt, false, false, false, true, 4, nullptr, nullptr, 4}},
        {"logic!", Type{TypeBool, false, false, false, true, 1, nullptr, nullptr, 1}},
    }};
    return datatypes;
}

const Type & Parser::findType(StringRef&& name) {
    static constexpr auto basic_types = genDatatypes();
    auto it = std::lower_bound(basic_types.begin(), basic_types.end(), name,
        [](const auto &a, const auto &b) {return -1 == a.first.compare_insensitive(b);});

    if (it == basic_types.end() || it->first != name) {
        
    } else {
        return it->second;
    }
}


const Type & Parser::parseType(Token *tok, Token **rest) {
    tok = skip(tok, TokenIdLBracket, '[');
    if (tok->id != TokenIdWord)
        comp.error(tok, err_expected, "datatype");
    auto type = findType(StringRef(tok->loc, tok->len));
    *rest = skip(tok->next, TokenIdRBracket, ']');
    return type;
}

// func-spec-block = "[" func-attrs-block? doc-string? param* return-spec? ("/local" local-var+)? "]"
Token *Parser::parseFuncSpec(Token *tok, Function *func) {
    tok = skip(tok, TokenIdLBracket, '[');
    if (consume(tok, TokenIdRBracket, &tok))
        return tok;

    // attribute block
    if (tok->id == TokenIdLBracket) {
        func->attrs = tok->next;
        func->n_attrs = skipBlock(tok, &tok) - 2;
        tok = tok->next;
    }

    // doc string?
    consume(tok, TokenIdString, &tok);

    // param* return-spec? ("/local" local-var+)?
    int n = 0;
    enum State {Param, Return, Local};
    State state = Param;
    while (tok->id != TokenIdRBracket) {
        // param = word "[" type "]" doc-string?
        if (tok->id == TokenIdWord) {
            if (state != Param)
                comp.error(tok, err_invalid_func);
            auto ident = tok;
            auto param = newNode<Type>(parseType(tok, &tok));
            param->tok = ident;
            consume(tok, TokenIdString, &tok);
        }

        // return-spec?
    }

    return tok;
}

// declaration = qualified-word: (function-decl | context-decl | alias-decl)
Node * Parser::parseDecl(Token *tok, Token **rest) {
    assert(tok->id == TokenIdSetWord);

    Token *ident = tok;
    StringRef name(tok->loc, tok->len);
    Node *node = nullptr;
    tok = tok->next;

    // function-decl = ("func" | "function") func-spec-block body-block
    if (tok->id == TokenIdFunc) {
        auto func = newNode<Function>(NodeFunc, ident);
        // func-spec-block
        tok = parseFuncSpec(tok->next, func);

        // body-block

        
    } else if (tok->id == TokenIdAlias) {

    } else if (tok->id == TokenIdContext) {

    }

    return node;

    // switch (tok->id)
    // {
    // case TokenIdFunc:
    //     break;
    // case TokenIdStruct:
    // case TokenIdAlias:
    // case TokenIdContext:
    // case TokenIdInteger:
    //     node = newNode<IntegerNode>(NodeInt, tok);
    //     break;
    // case TokenIdFloat:
    // case TokenIdString:
    // case TokenIdTrue:
    // case TokenIdFalse:
    // case TokenIdByte:
    // case TokenIdBinary:
    // case TokenIdLBracket:   // literal array
    // case TokenIdWord:
    // case TokenIdGetWord:
    // default:
    //     break;
    // }

    return node;
}

// 1. Collect function, import function, alias, context and enum
// 2. Try to collect global variable as many as possible
Token * Parser::collectDeclarations(Token *tok, Context *ctx, bool is_block) {
    SmallVector<Token *> bracket;
    SmallVector<Token *> paren;
    Token head = {};
    Token *cur = &head;

    bool skip_toks = false;
    Node *node = nullptr;

    while (tok->id != TokenIdEof) {
        switch (tok->id)
        {
        case TokenIdSetWord:
            if (node = parseDecl(tok, &tok); node)
                addNode(ctx, StringRef(tok->loc, tok->len), node);
            break;
        case TokenIdWord:
            break;    
        case TokenIdMacroEnum:
            break;
        case TokenIdMacroImport:
            break;
        case TokenIdLBracket: 
            bracket.push_back(tok); break;
        case TokenIdRBracket:
                if (bracket.empty())
                    comp.error(tok, err_dangling, ']');
                bracket.pop_back();
                if (bracket.empty()) {
                    if (paren.size()) {
                        comp.error(paren.back(), err_expected_match, ')', '(');
                    }
                    if (is_block) return tok;
                }
                break;
        case TokenIdLParen:
            paren.push_back(tok); break;
        case TokenIdRParen:
            if (paren.empty())
                comp.error(tok, err_dangling, ')');
            paren.pop_back(); 
            if (paren.empty()) {
                if (bracket.size()) {
                    comp.error(bracket.back(), err_expected_match, ']', '[');
                }
            }
            break;
        default:
            break;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }

    return head.next;
}

Program * Parser::parse(Token *tok) {
    Context *ctx = newNode<Context>(NodeContext, tok);
    program->ctx = ctx;

    tok = collectDeclarations(tok, ctx);

    return nullptr;
}
