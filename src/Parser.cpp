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
 *  enum              = "#enum" word "[" (word | (word:+ integer))+ "]"
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
 *  struct-type       = struct! "[" typed-word+ "]" "value"?
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

void Parser::addDecl(Context *ctx, const StringRef& name, Node *node) {
    auto& decls = ctx->decls;
    auto&& [keyval, is_new] = decls.insert({name, node});
    if (!is_new)
        comp.error(name.data(), err_symbol_declared, name);
}

Node * Parser::findDecl(StringRef name) {
    auto ctx = curCtx;
    do {
        auto& decls = ctx->decls;
        auto it = decls.find(name);
        if (it != decls.end())
            return it->second;
        else
            ctx = ctx->parent;
    } while (ctx);
    return nullptr;
}

constexpr auto genDatatypes()
{
    std::array<std::pair<StringRef, Type>, 6> datatypes = {{
        {"byte!", Type{TypeByte, false, false, 1, 1, nullptr, nullptr, 3}},
        {"c-string!", Type{TypePtr, false, false, 8, 8, nullptr, nullptr, TypePtr}},
        {"float!", Type{TypeDouble, false, false, 8, 8, nullptr, nullptr, TypeDouble}},
        {"float32!", Type{TypeFloat, false, false, 4, 4, nullptr, nullptr, TypeFloat}},
        {"integer!", Type{TypeInt, true, false, 4, 4, nullptr, nullptr, TypeInt}},
        {"logic!", Type{TypeBool, true, false, 1, 1, nullptr, nullptr, TypeBool}},
    }};
    return datatypes;
}

const Type * Parser::findType(const StringRef& name) {
    static constexpr auto basic_types = genDatatypes();
    auto it = std::lower_bound(basic_types.begin(), basic_types.end(), name,
        [](const auto &a, const auto &b) {return -1 == a.first.compare_insensitive(b);});

    if (it == basic_types.end() || it->first != name) {
        auto node = findDecl(name);
        if (node && node->id < TypeType) return (Type *)node;
    } else {
        return const_cast<Type *>(&it->second);
    }
    return nullptr;
}

const Type * Parser::parseType(Token *tok, Token **rest, bool *has_value, bool is_block, bool is_collect) {
    if (is_block) tok = skip(tok, TokenIdLBracket, '[');
    if (tok->id != TokenIdWord)
        comp.error(tok, err_expected, "datatype");
    const Type *type = findType(tok->str());
    if (type) {
        tok = tok->next;
    } else {
        if (tok->equals("struct!")) {
            type = parseStruct(tok, &tok, is_collect);
        } else if (tok->equals("function!")) {
            auto fn = newNode<Function>(TypeFunc);
            tok = parseFuncSpec(tok, fn, is_collect);
            type = fn;
        } else if (tok->equals("pointer!")) {
            bool no_value = false;
            type = newPtrType(parseType(tok->next, &tok, &no_value, true, is_collect));
        } else {
            comp.error(tok, err_invalid_type);
        }
    }
    bool value = false;
    if (tok->id == TokenIdWord && tok->equals("value")) {
        value = true;
        if (*has_value && type->id == TypeStruct) {
            tok = tok->next;
        } else {
            comp.error(tok, err_invalid_type);
        }
    }
    *has_value = value;
    if (is_block) tok = skip(tok, TokenIdRBracket, ']');
    *rest = tok;
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
            bool is_struct_value = true;
            auto type = parseType(tok, &tok, &is_struct_value);
            while (n--) {
                cur = cur->next = newNode<Type>(type);
                cur->tok = name;
                if (is_struct_value) cur->id = TypeStructValue;
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
Token *Parser::parseFuncSpec(Token *tok, Function *func, bool is_collect) {
    if (is_collect) {
        func->spec = tok;
        skipBlock(tok, &tok);
        return tok;
    }

    tok = skip(tok, TokenIdLBracket, '[');
    if (consume(tok, TokenIdRBracket, &tok))
        return tok;

    // attribute block
    if (tok->id == TokenIdLBracket) {
        func->attrs = tok;
        skipBlock(tok, &tok);
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
            bool is_struct_value = true;
            auto param = newNode<Type>(parseType(tok->next, &tok, &is_struct_value));
            if (is_struct_value) param->id = TypeStructValue;
            param->tok = ident;
            func->params = func->params->next = param;
            func->n_params++;
            consume(tok, TokenIdString, &tok);

        } else if (state < Return && tok->id == TokenIdReturn && tok->next->id == TokenIdSetColon) {
            // return-spec? = "[" type "]" doc-string?
            state = Return;
            bool is_struct_value = true;
            func->return_type = parseType(tok->next->next, &tok, &is_struct_value);
            if (is_struct_value) {
                auto type = newNode<Type>(func->return_type);
                type->id = TypeStructValue;
                func->return_type = type;
            }
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

Node *Parser::parseMembers(Token *tok, Token **rest) {
    tok = skip(tok, TokenIdLBracket, '[');
    Node head{};
    Node *cur = &head;
    uint32_t idx = 0;
    while (tok->id != TokenIdEof) {
        // typed-word = word "[" type "]"
        if (tok->id == TokenIdWord || tok->sub_id == TokenIdKeyword) {
            Token *tag = tok;
            bool is_struct_value = true;
            const Type *type = parseType(tok->next, &tok, &is_struct_value);
            Member *member = newNode<Member>();
            *(Type *)member = *type;
            if (is_struct_value) member->id = TypeStructValue;
            member->tok = tag;
            member->idx = idx++;
            cur = cur->next = member;
        } else if (tok->id == TokenIdRBracket) {
            tok = tok->next;
            break;
        } else {
            comp.error(tok, err_invalid_struct, tok->str());
        }
    }
    if (!idx) comp.error(tok, err_invalid_struct, tok->str());  // empty struct
    return head.next;
}

Type * Parser::parseStruct(Token *tok, Token **rest, bool is_collect) {
    assert(tok->equals("struct!"));
    // struct-type = struct! "[" typed-word+ "]"
    Type *st = newNode<Type>(TypeStruct, tok);
    tok = tok->next;
    if (is_collect) {
        st->id = TypeStructUnresolved;
        st->data.tokens = tok;
        skipBlock(tok, rest);
        return st;
    }
    st->data.member = parseMembers(tok, rest);
    return st;
}

// declaration = qualified-word: (function-decl | context-decl | alias-decl)
Node * Parser::collectDecl(Token *tok, Token **rest) {
    assert(tok->id == TokenIdSetWord);

    *rest = tok;
    Token *name = tok;
    Node *node = nullptr;
    tok = tok->next;

    // function-decl = ("func" | "function") func-spec-block body-block
    if (tok->id == TokenIdFunc) {
        auto func = newNode<Function>(NodeFunc, name);
        // func-spec-block
        tok = parseFuncSpec(tok->next, func, true);

        // body-block
        func->body = tok;
        skipBlock(tok, rest);
        name->next->next = *rest;
        return func;
    }

    // alias-decl = "alias" type
    if (tok->id == TokenIdAlias) {
        tok = tok->next;
        expect(tok, TokenIdWord, "word");
        bool no_value = false;
        auto type = (Type *)collectType(tok, rest, &no_value, false);
        if (!type->tid) type->tid = curTypeId++;
        name->next->next = *rest;
        return node;
    }

    if (tok->id == TokenIdContext) {
        Context *ctx = newContext(tok);
        ctx->parent = curCtx;
        curCtx = ctx;
        expect(tok->next, TokenIdLBracket, '[');
        *rest = collectDeclarations(tok->next, ctx, true);
        curCtx = ctx->parent;
        return ctx;
    }

    // invalid cases
    if (tok->id == TokenIdSlash)
        comp.error(name, err_invalid_token);

    return node;
}

Token * Parser::parseEnum(Token *tok, Context *ctx) {
    assert(tok->id == TokenIdMacroEnum);
    // enum = "#enum" word "[" (word | (word:+ integer))+ "]"

    Token *main_tok = tok;
    tok = tok->next;
    Type *node = newNode<Type>(NodeEnum, tok);
    addDecl(ctx, StringRef(tok->loc, tok->len), node);

    tok = skip(tok, TokenIdWord, "word");
    tok = skip(tok, TokenIdLBracket, '[');

    int64_t val = 0;
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdWord && head.next == nullptr) {
            if (tok->sub_id == TokenIdKeyword)
                comp.error(tok, err_redefine_keyword, tok->str());
            Type *elem = newNode<Type>(NodeEnum, tok);
            elem->next = node;
            elem->data.value = val++;
            addDecl(ctx, tok->str(), elem);
        } else if (tok->id == TokenIdSetWord) {
            Type *elem = newNode<Type>(NodeEnum, tok);
            cur = cur->next = elem;
        } else if ((tok->id == TokenIdHex || tok->id == TokenIdInteger) && head.next) {
            tok->str().getAsInteger(tok->id == TokenIdHex ? 16 : 10, val);
            cur = head.next;
            do {
                Type *elem = (Type *)cur;
                cur = cur->next;
                elem->next = node;
                elem->data.value = val;
                addDecl(ctx, elem->tok->str(), elem);
            } while (cur);
            head.next = nullptr;
            cur = &head;
        } else if (tok->id == TokenIdRBracket) {
            tok = tok->next;
            break;
        } else {
            comp.error(tok, err_invalid_enum, tok->str());
        }
        tok = tok->next;
    }
    main_tok->next = tok;
    return tok;
}

void Parser::resolveStructs(Context *ctx) {
    curCtx = ctx;
    for (auto const &val : ctx->decls) {
        Type *ty = (Type *)val.second;
        if (ty->id == TypePtr) ty = (Type *)ty->data.type;  // maybe a struct pointer
        if (ty->id == TypeStructUnresolved) {
            Token *rest;
            ty->data.member = parseMembers(ty->data.tokens, &rest);
            ty->id = TypeStruct;
        } else if (ty->id == NodeContext) {
            resolveStructs((Context *)ty);
            curCtx = ctx;
        }
    }
}

// 1. Collect function, import function, alias, context and enum
// 2. Try to collect global variable as many as possible
// 3. Recognize path!
Token * Parser::collectDeclarations(Token *tok, Context *ctx, bool is_block) {
    SmallVector<std::pair<Token *, uint32_t>> bracket;
    SmallVector<std::pair<Token *, uint32_t>> paren;
    Token head = {};
    Token *cur = &head;

    uint32_t level;
    Node *node = nullptr;
    Token *rest = nullptr;

    while (tok->id != TokenIdEof) {
        bool skip_decl = false;
        switch (tok->id)
        {
        case TokenIdWord:
            if (tok->next->id == TokenIdSlash) {    // a path
                tok->id = TokenIdPath;
                cur = cur->next = tok;
                tok = tok->next->next;
                continue;
            }
            break;
        case TokenIdSetWord:
            node = collectDecl(tok, &rest);
            if (node) {
                addDecl(ctx, tok->str(), node);
                if (cur->id == TokenIdPath)
                    comp.error(tok->next, err_cannot_declared, tok->next->str());
                skip_decl = true;
            }
            break;
        case TokenIdMacroEnum:
            rest = parseEnum(tok, ctx);
            skip_decl = true;
            break;
        case TokenIdMacroImport:
            skip_decl = true;
            break;
        case TokenIdLBracket: 
            bracket.push_back({tok, paren.size()}); break;
        case TokenIdRBracket:
                if (bracket.empty())
                    comp.error(tok, err_dangling, ']');
                level = bracket.back().second;
                bracket.pop_back();
                if (bracket.empty()) {
                    if (paren.size() != level) {
                        comp.error(paren.back().first, err_expected_match, ')', '(');
                    }
                    if (is_block) {
                        cur->next = tok;
                        return tok->next;
                    }
                }
                break;
        case TokenIdLParen:
            paren.push_back({tok, bracket.size()}); break;
        case TokenIdRParen:
            if (paren.empty())
                comp.error(tok, err_dangling, ')');
            level = paren.back().second;
            paren.pop_back(); 
            if (paren.empty()) {
                if (bracket.size() != level) {
                    comp.error(bracket.back().first, err_expected_match, ']', '[');
                }
            }
            break;
        default:
            break;
        } 
        if (skip_decl) {
            cur->next = tok;
            while (tok->next != rest) tok = tok->next;
            cur = tok;
        } else {
            cur = cur->next = tok;
        }
        tok = tok->next;
    }

    return head.next;
}

Program * Parser::parse(Token *tok) {
    Program *program = newNode<Program>(NodeProgram, tok);
    Context *ctx = newContext(tok);
    program->ctx = curCtx = ctx;
    Node func{};
    Node global{};
    curFunc = &func;
    curGlobal = &global;

    tok = collectDeclarations(tok, ctx);
dumpTokens(tok, "After collectDeclarations");

    resolveStructs(ctx);

    program->functions = func.next;
    program->data = global.next;
    return program;
}
