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

void Parser::addDecl(Context *ctx, StringRef&& name, Node *node) {
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

const Type * Parser::findType(StringRef&& name) {
    static constexpr auto basic_types = genDatatypes();
    auto it = std::lower_bound(basic_types.begin(), basic_types.end(), name,
        [](const auto &a, const auto &b) {return -1 == a.first.compare_insensitive(b);});

    if (it == basic_types.end() || it->first != name) {
        
    } else {
        return const_cast<Type *>(&it->second);
    }
}

const Type * Parser::parseType(Token *tok, Token **rest) {
    tok = skip(tok, TokenIdLBracket, '[');
    if (tok->id != TokenIdWord)
        comp.error(tok, err_expected, "datatype");
    const Type *type = findType(StringRef(tok->loc, tok->len));
    *rest = skip(tok->next, TokenIdRBracket, ']');
    return type;
}

Token * Parser::parseLocal(Token *tok, Function *func) {
    uint32_t n = 0;
    uint32_t total = 0;
    Node head{};
    Node *cur = &head;
    Token ident{};
    Token *t = &ident;
    while (tok->id != TokenIdRBracket) {
        if (tok->id == TokenIdWord) {
            t = t->next = tok;
            tok = tok->next;
            n++;
        } else if (tok->id == TokenIdLBracket && n > 0) {
            total += n;
            auto name = ident.next;
            auto type = parseType(tok, &tok);
            while (n--) {
                cur = cur->next = newNode<Type>(type);
                cur->tok = name;
                name = name->next;
            }
            t = &ident;
            n = 0;
        } else {
            comp.error(tok, err_invalid_func);
        }
    }
    func->locals = head.next;
    func->n_locals = total;
    return tok;
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
    enum State {Param, Return, Local};
    State state = Param;
    Node params{};
    func->params = &params;
    while (tok->id != TokenIdRBracket) {
        if (state == Param && tok->id == TokenIdWord) {
            // param = word "[" type "]" doc-string?
            auto ident = tok;
            auto param = newNode<Type>(parseType(tok->next, &tok));
            param->tok = ident;
            func->params = func->params->next = param;
            func->n_params++;
            consume(tok, TokenIdString, &tok);

        } else if (state < Return && tok->id == TokenIdReturn && tok->next->id == TokenIdSetColon) {
            // return-spec? = "[" type "]" doc-string?
            state = Return;
            func->return_type = parseType(tok->next->next, &tok);
            consume(tok, TokenIdString, &tok);

        } else if (state < Local && tok->id == TokenIdRefinement && tok->equals("local")) {
            //local-var = word+ ("[" type "]")?
            state = Local;
            tok = parseLocal(tok->next->next, func);
        } else {
            comp.error(tok, err_invalid_func);
        }
    }
    func->params = params.next;

    return tok->next;
}

// declaration = qualified-word: (function-decl | context-decl | alias-decl)
Node * Parser::parseDecl(Token *tok, Token **rest) {
    assert(tok->id == TokenIdSetWord);

    *rest = tok;
    Token *name = tok;
    Node *node = nullptr;
    tok = tok->next;

    // function-decl = ("func" | "function") func-spec-block body-block
    if (tok->id == TokenIdFunc) {
        auto func = newNode<Function>(NodeFunc, name);
        // func-spec-block
        tok = parseFuncSpec(tok->next, func);

        // body-block
        expect(tok, TokenIdLBracket, '[');
        func->body = tok->next;
        func->n_body = skipBlock(tok, rest) - 2;    // don't count [ ]
        return func;
    } 

    // alias-decl = "alias" type
    if (tok->id == TokenIdAlias) {
        
    }
    
    if (tok->id == TokenIdContext) {
        Context *ctx = newNode<Context>(NodeContext, tok);
        ctx->parent = curCtx;
        curCtx = ctx;
        expect(tok->next, TokenIdLBracket, '[');
        *rest = collectDeclarations(tok->next, ctx, true);
        return ctx;
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
}

// 1. Collect function, import function, alias, context and enum
// 2. Try to collect global variable as many as possible
Token * Parser::collectDeclarations(Token *tok, Context *ctx, bool is_block) {
    SmallVector<Token *> bracket;
    SmallVector<Token *> paren;
    Token head = {};
    Token *cur = &head;

    bool skip_decl = true;
    Token *rest = nullptr;
    Node *node = nullptr;

    while (tok->id != TokenIdEof) {
        switch (tok->id)
        {
        case TokenIdSetWord:
            if (node = parseDecl(tok, &rest); node) {
                addDecl(ctx, StringRef(tok->loc, tok->len), node);
                if (!skip_decl) {                
                    cur->next = tok;
                    while (tok->next != rest) tok = tok->next;
                    cur = tok;
                }
                tok = rest;
                skip_decl = true;
                continue;
            }
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
                    if (is_block) return tok->next;
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
            skip_decl = false;
            break;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }

    return head.next;
}

Program * Parser::parse(Token *tok) {
    Context *ctx = newNode<Context>(NodeContext, tok);
    program->ctx = curCtx = ctx;

    tok = collectDeclarations(tok, ctx);
    auto tk = tok;
    llvm::errs() << "\n\nAfter collectDeclarations\n";
    while (tk) {
        llvm::errs() << TokenNames[tk->id] << ":" << tk->str() << " ";
        tk = tk->next;
    }

    return program;
}
