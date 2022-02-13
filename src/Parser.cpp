/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "Parser.h"
#include "CodeGen.h"
#include "Compilation.h"

/** Grammar
 * 
 *  code              = (declaration | statement | expression)*
 * 
 *  declaration       = (word: (function-decl | context-decl | alias-decl)) | syscall | import | enum
 *  function-decl     = ("func" | "function") func-spec-block body-block
 *  context-decl      = "context" "[" program "]"
 *  alias-decl        = "alias" type
 *  syscall           = "#syscall" "[" word: integer func-spec-block "]"
 *  import            = "#import" "[" library-functions* "]"
 *  enum              = "#enum" word "[" (word | (word:+ integer))+ "]"
 * 
 *  func-spec-block   = "[" func-attrs-block? doc-string? param* return-spec? ("/local" local-var+)? "]"
 *  body-block        = "[" (word: alias-decl) | syscall | import | enum | statement | expression)* "]"
 *  func-attrs-block  = "[" "infix" | "catch" | "callback" | "custom" | "stdcall" | "cdecl" (variadic | typed)? "]"
 *  param             = typed-word doc-string?
 *  typed-word        = word "[" type "]"
 *  local-var         = word+ ("[" type "]")?
 *  return-spec       = "return:" "[" type "]"
 *  qualified-word    = ((word/)*)word
 *  path              = path-head/(word | integer)
 *  path-head         = word | path
 * 
 *  library-functions = string ("stdcall" | "cdecl") "[" (word: string spec-block)* "]"
 *  spec-block        = func-spec-block | var-spec-block
 *  var-spec-block    = "[" type "]"
 * 
 *  statement         = conditional-stmt | return-stmt | exception-stmt | with-stmt | use-stmt | assignment
 * 
 *  conditional-stmt  = if-stmt | either-stmt | case-stmt | switch-stmt | while-stmt | until-stmt
 *  if-stmt           = "if" cond-expr body-block
 *  either-stmt       = "either" cond-expr body-block body-block
 *  case-stmt         = "case" "[" (cond-expr body-block)+ "]"
 *  switch-stmt       = "switch" expression "[" (selectors body-block)* ("default" body-block)? "]"
 *  while-stmt        = "while" cond-block body-block
 *  until-stmt        = "until" cond-block 
 *  return-stmt       = "exit" | "return" expression
 *  exception-stmt    = catch | throw
 *  with-stmt         = "with" (qualified-word | "[" qualified-word+ "]") body-block
 *  use-stmt          = "use" "[" param+ "]" body-block
 *  cond-expr         = expression
 *  selectors         = (integer-literal | byte-literal)+
 * 
 *  catch             = "catch" filter body-block handler
 *  filter            = integer!
 *  handler           = body-block
 * 
 *  assignment        = lh-side+ expression
 *  lh-side           = qualified-word: | path:
 * 
 *  expression        = builtin | func-call | type-cast | short-circuit | binary-operation | value
 * 
 *  builtin           = declare | size-enquiry | not
 *  declare           = "declare" type
 *  size-enquiry      = "size?" (type | string-literal)
 *  not               = not expression
 * 
 *  func-call         = call-fixed-arg | call-variadic
 *  call-fixed-arg    = qualified-word expression*
 *  call-variadic     = qualified-word (expression | "[" expression+ "]")
 * 
 *  type-cast         = "as" (type | "[" type "]") "keep"? expression
 *  type              = integer! | byte! |  logic! | c-string! | float! | float32! | float64! | subroutine! |
 *                      func-type | pointer-type | struct-type | alias-name | enum-type-name
 *  func-type         = function! func-spec-block
 *  pointer-type      = pointer! "[" type "]"
 *  struct-type       = struct! "[" typed-word+ "]" "value"?
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
 *  value             = literal | "(" expression ")" | word | path | get-word | get-path
 * 
 **/

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

enum TypeConvResult : uint8_t {
    tc_same,      // same type
    tc_i_ii,      // int to int: explicit and implicit conversion allowed
    tc_i_if,      // int to float: explicit and implicit conversion allowed
    tc_i_ff,      // @@ make sure it's the last one in this group. float to float: explicit and implicit conversion allowed

    tc_e_ii,      // int to int: explicit conversion only
    tc_e_if,      // int to float: explicit conversion only
    tc_e_ff,      // float to float: explicit conversion only
    tc_e_fi,      // float to int: explicit conversion only
    tc_e_xp,      // other types to ptr: explicit conversion only
    tc_e_px,      // ptr to other types: explicit conversion only
    tc_e_ss,      // struct to struct
    tc_fail,      // cannot convert
};

// TypeConv[from][to]
static const TypeConvResult TypeConv[TypeType][TypeType] = {
// To: void    null     bool     byte     i32      f32      f64      enum     ptr     struct
    {tc_same, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail},  // From void
    {tc_fail, tc_same, tc_e_ii, tc_e_ii, tc_e_ii, tc_fail, tc_fail, tc_e_ii, tc_same, tc_fail},  // From null
    {tc_fail, tc_fail, tc_same, tc_e_ii, tc_e_ii, tc_fail, tc_fail, tc_e_ii, tc_e_xp, tc_fail},  // From bool
    {tc_fail, tc_fail, tc_e_ii, tc_same, tc_i_ii, tc_fail, tc_fail, tc_e_ii, tc_e_xp, tc_fail},  // From byte
    {tc_fail, tc_fail, tc_e_ii, tc_e_ii, tc_same, tc_e_if, tc_i_if, tc_e_ii, tc_e_xp, tc_fail},  // From i32
    {tc_fail, tc_fail, tc_fail, tc_fail, tc_e_fi, tc_same, tc_i_ff, tc_e_fi, tc_fail, tc_fail},  // From f32
    {tc_fail, tc_fail, tc_fail, tc_fail, tc_e_fi, tc_e_ff, tc_same, tc_e_fi, tc_fail, tc_fail},  // From f64
    {tc_fail, tc_fail, tc_e_ii, tc_e_ii, tc_i_ii, tc_e_if, tc_e_if, tc_same, tc_e_xp, tc_fail},  // From enum
    {tc_fail, tc_fail, tc_e_px, tc_e_px, tc_e_px, tc_fail, tc_fail, tc_fail, tc_same, tc_fail},  // From ptr
    {tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_fail, tc_e_ss},  // From struct
};

Variable * Parser::addVar(Context *ctx, Token *name, bool is_local) {
    auto node = newNode<Variable>(NodeVar, name);
    auto& vars = ctx->decls;
    auto const & [keyval, is_new] = vars.insert({name->str(), node});
    if (!is_new && is_local)
        comp.error(name, err_symbol_declared, name->str());
    return node;
}

void Parser::addDecl(Context *ctx, Token *name, Node *node) {
    auto& decls = ctx->decls;
    auto const & [keyval, is_new] = decls.insert({name->str(), node});
    if (!is_new)
        comp.error(name, err_symbol_declared, name->str());
}

Node * Parser::findDecl(Token *name, Context **ctx_out) {
    auto ctx = curCtx;
    do {
        auto& decls = ctx->decls;
        auto it = decls.find(name->str());
        if (it != decls.end()) {
            *ctx_out = ctx;
            return it->second;
        } else {
            ctx = ctx->parent;
        }
    } while (ctx);
    return nullptr;
}

Variable * Parser::findVar(Token *name, Context **ctx_out) {
    return (Variable *)findDecl(name, ctx_out);
}

Context * Parser::findContext(Token *tok) {
    Context *ctx_out = nullptr;
    Context *ctx = (Context *)findDecl(tok, &ctx_out);
    if (ctx && ctx->id == NodeContext)
        return ctx;
    return nullptr;
}

constexpr auto genDatatypes()
{
    std::array<std::pair<StringRef, Type>, 6> datatypes = {{
        {"byte!", Type{TypeByte, false, 1, 1, nullptr, nullptr, TypeByte}},
        {"c-string!", Type{TypePtr, false, 8, 8, nullptr, nullptr, TypePtr}},
        {"float!", Type{TypeDouble, false, 8, 8, nullptr, nullptr, TypeDouble}},
        {"float32!", Type{TypeFloat, false, 4, 4, nullptr, nullptr, TypeFloat}},
        {"integer!", Type{TypeInt, true, 4, 4, nullptr, nullptr, TypeInt}},
        {"logic!", Type{TypeBool, false, 1, 1, nullptr, nullptr, TypeBool}},
    }};
    return datatypes;
}

const Type * Parser::findType(Token *name, Context **ctx) {
    static constexpr auto basic_types = genDatatypes();
    auto it = std::lower_bound(basic_types.begin(), basic_types.end(), name->str(),
        [](const auto &a, const auto &b) {return -1 == a.first.compare_insensitive(b);});

    if (it == basic_types.end() || !name->equals(it->first)) {
        auto node = findDecl(name, ctx);
        if (node && node->id < TypeType) return (Type *)node;
    } else {
        return const_cast<Type *>(&it->second);
    }
    return nullptr;
}

const Type * Parser::parseType(Token *tok, Token **rest, bool is_block, bool is_collect) {
    if (is_block) tok = skip(tok, TokenIdLBracket, '[');
    if (tok->id != TokenIdWord)
        comp.error(tok, err_expected, "datatype");
    Context *ctx = nullptr;
    const Type *type = findType(tok, &ctx);
    if (type) {  // alias type
        tok = tok->next;
        if (type->id == TypeStructUnresolved && !is_collect && tok->id == TokenIdWord && tok->equals("value")) { // struct value
            resolveStruct((Type *)type, ctx);
            tok = tok->next;
        } else if (type->id > TypePtr && !is_collect) {
            type = (Type *)newPtrType(type);
        }
    } else {     // anonymous type
        if (tok->equals("struct!")) {
            type = parseStruct(tok, &tok, is_collect);
        } else if (tok->equals("function!")) {
            auto fn = newNode<Function>(TypeFunc);
            tok = parseFuncSpec(tok, fn, is_collect);
            type = fn;
        } else if (tok->equals("pointer!")) {
            type = newPtrType(parseType(tok->next, &tok, true, is_collect));
        } else {
            comp.error(tok, err_invalid_type);
        }
    }
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
            auto type = parseType(tok, &tok);
            while (n) {
                cur = cur->next = copyNode<Type>(type);
                cur->tok = name;
                name = name->next;
                n--;
            }
            t = &ident;
        } else {
            comp.error(tok, err_invalid_func);
        }
    }
    if (n) {    // untyped variables
        total += n;
        auto name = ident.next;
        while (n--) {
            cur = cur->next = newNode<Node>(TypeVoid, name);
            name = name->next;
        }
    }
    func->locals = head.next;
    func->n_locals = total;
    return tok;
}

// func-spec-block = "[" func-attrs-block? doc-string? param* return-spec? ("/local" local-var+)? "]"
Token *Parser::parseFuncSpec(Token *tok, Function *func, bool is_collect) {
    if (is_collect) {
        func->id = TypeFuncUnresolved;
        func->spec = tok;
        skipBlock(tok, &tok);
        return tok;
    }

    func->id = TypeFunc;
    tok = skip(tok, TokenIdLBracket, '[');
    if (consume(tok, TokenIdRBracket, &tok))
        return tok;

    // attribute block
    if (tok->id == TokenIdLBracket) {
        auto attrs = func->attrs;
        do {
            using namespace llvm;
            tok = tok->next;
            if (tok->equals("variadic"))     attrs |= FuncVariadic;
            else if (tok->equals("cdecl"))   attrs = attrs & (~FuncCallConv) | FuncCdecl;
            else if (tok->equals("stdcall")) attrs = attrs & (~FuncCallConv) | FuncStdcall;
            else if (tok->id == TokenIdRBracket) break;
            else comp.error(tok, err_invalid_func);
        } while (tok->id != TokenIdEof);
        func->attrs = attrs;
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
            auto param = copyNode<Type>(parseType(tok->next, &tok));
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
            tok = parseLocal(tok->next, func);
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
            const Type *type = parseType(tok->next, &tok);
            Member *member = newNode<Member>();
            *(Type *)member = *type;
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
    // struct-type = struct! "[" typed-word+ "]" "value"?
    Type *st = newNode<Type>(TypeStruct, tok);
    tok = tok->next;
    if (is_collect) {
        st->id = TypeStructUnresolved;
        st->data.st_body = tok;
        skipBlock(tok, rest);
        return st;
    }

    skipBlock(tok, rest);
    if ((*rest)->id == TokenIdWord && (*rest)->equals("value")) {
        st->data.member = parseMembers(tok, &tok);
        *rest = tok->next;  // skip token "value"
        return st;
    } else {
        return newPtrType(st);
    }
}

// declaration = qualified-word: (function-decl | context-decl | alias-decl)
Node * Parser::collectDecl(Token *tok, Token **rest) {
    assert(tok->id == TokenIdSetWord || tok->id == TokenIdSetColon);

    *rest = tok;
    Token *name = tok;
    tok = tok->next;

    // function-decl = ("func" | "function") func-spec-block body-block
    if (tok->id == TokenIdFunc) {
        auto func = newNode<Function>(TypeFunc, name);
        func->import_name = name;
        // func-spec-block
        tok = parseFuncSpec(tok->next, func, true);

        // body-block
        func->body.token = tok;
        skipBlock(tok, rest);
        name->next->next = *rest;
        return func;
    }

    // alias-decl = "alias" type
    if (tok->id == TokenIdAlias) {
        tok = tok->next;
        expect(tok, TokenIdWord, "word");
        auto type = (Type *)collectAlias(tok, rest);
        type->tok = name;
        if (!type->tid) type->tid = curTypeId++;
        name->next->next = *rest;
        return type;
    }

    if (tok->id == TokenIdContext) {
        Context *ctx = newContext(name);
        ctx->parent = curCtx;
        curCtx = ctx;
        expect(tok->next, TokenIdLBracket, '[');
        *rest = collectDeclarations(tok->next, ctx);
        curCtx = ctx->parent;
        return ctx;
    }

    if (name->id == TokenIdSetWord) {
        Context *ctx;
        auto node = findVar(name, &ctx);
        if (!node) node = addVar(curCtx, name);
        else if (node->id != NodeVar) comp.error(name, err_symbol_declared, name->str());
    }

    // invalid cases
    if (tok->id == TokenIdSlash)
        comp.error(name, err_invalid_token);

    return nullptr;
}

Token * Parser::parseEnum(Token *tok, Context *ctx) {
    assert(tok->id == TokenIdMacroEnum);
    // enum = "#enum" word "[" (word | (word:+ integer))+ "]"

    Token *main_tok = tok;
    tok = tok->next;
    Type *node = newNode<Type>(TypeEnum, tok);
    node->tid = curTypeId++;
    addDecl(ctx, tok, node);

    tok = skip(tok, TokenIdWord, "word");
    tok = skip(tok, TokenIdLBracket, '[');

    int64_t val = 0;
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdWord && head.next == nullptr) {
            Type *elem = newNode<Type>(TypeEnum, tok);
            elem->next = node;
            elem->tid = node->tid;
            elem->data.value = val++;
            addDecl(ctx, tok, elem);
        } else if (tok->id == TokenIdSetWord) {
            Type *elem = newNode<Type>(TypeEnum, tok);
            cur = cur->next = elem;
        } else if ((tok->id == TokenIdHex || tok->id == TokenIdInteger) && head.next) {
            tok->toInteger(val);
            cur = head.next;
            do {
                Type *elem = (Type *)cur;
                cur = cur->next;
                elem->next = node;
                elem->tid = node->tid;
                elem->data.value = val;
                addDecl(ctx, elem->tok, elem);
            } while (cur);
            head.next = nullptr;
            cur = &head;
        } else if (tok->id == TokenIdRBracket) {
            tok = tok->next;
            break;
        } else if (tok->sub_id == TokenIdKeyword) {
            comp.error(tok, err_redefine_keyword, tok->str());
        } else {
            comp.error(tok, err_invalid_enum, tok->str());
        }
        tok = tok->next;
    }
    main_tok->next = tok;
    return tok;
}

bool Parser::resolveStruct(Type *ty, Context *ctx) {
    if (ty->id == TypeStructUnresolved) {
        auto savedCtx = curCtx;
        curCtx = ctx;
        Token *rest;
        if (ty->tid) {
            if (std::find(tids.begin(), tids.end(), ty->tid) != tids.end())
                comp.error(ty->tok, err_circular_ref);
            tids.push_back(ty->tid);
        }
        ty->data.member = parseMembers(ty->data.st_body, &rest);
        ty->id = TypeStruct;
        if (ty->tid) tids.pop_back();
        curCtx = savedCtx;
        return true;
    }
    return false;
}

void Parser::resolveDeclarations(Context *ctx) {
    curCtx = ctx;
    for (auto const &val : ctx->decls) {
        Type *ty = (Type *)val.second;
        if (!resolveStruct(ty, ctx) && ty->id == NodeContext) {
            resolveDeclarations((Context *)ty);
            curCtx = ctx;
        } else if (ty->id == TypeFuncUnresolved) {
            Function *fn = (Function *)ty;
            parseFuncSpec(fn->spec, fn);
            dumpFunction(fn);
        }
    }
}

const Type * Parser::addVarType(Node *node) {
    if (node->id == NodeAssign) {
        Assignment *assign = (Assignment *)node;
        Variable *var = (Variable *) assign->lhs;
        const Type *ty = addVarType(assign->rhs);
        if (!var->type || var->type->id == TypeVoid)
            var->type = ty;
        //else check type
        LOG() << var->tok->str() << " " << TypeName[var->type->id] << '\n';
        return ty;
    }
    else if (node->id < TypeType) return (Type *)node;
    else if (node->id == NodeFnCall) return ((FnCall*)node)->fn->return_type;
    else if (node->id == NodeVar) return ((Variable*)node)->type;
    else comp.error(node->tok, err_expected, "expression");
}

void Parser::addTypeInfo(Node *node) {
    while (node) {
        LOG() << NodeName[node->id] << '\n';
        switch (node->id)
        {
        case NodeAssign:
            addVarType(node);
            break;
        case NodeIf:
            addTypeInfo(((If *)node)->body);
            break;
        case NodeFnCall:
            addTypeInfo(((FnCall *)node)->args);
            break;
        case NodeIdent:
            if (((Identifier *)node)->is_type(TypeVoid))
                error(node, err_not_initialized, node->str());
            break;
        case NodePtr:
            if (((Pointer *)node)->is_type(TypeVoid))
                error(node, err_not_initialized, node->str());
            break;
        default:
            break;
        }
        node = node->next;
    }
}

void Parser::updateCall(Function *fn, FnCall *call, bool is_main_func) {
    Variable *var;
    if (fn->isThrowable()) {
        var = is_main_func ? (Variable *)fn->locals : (Variable *)fn->params;
    } else {
        var = newNode<Variable>(NodeVar, &hiddenParamTok);
        if (is_main_func) {
            var->type = &uintTy;
            // add a hidden local variable in caller for exception handling
            var->next = fn->locals;
            fn->locals = var;
            fn->n_locals++;
        } else {
            var->type = &uintPtrTy;
            // add a hidden param in caller for exception handling
            var->next = fn->params;
            fn->params = var;
            fn->n_params++;
        }
        fn->addAttr(FuncThrow);
    }
    Node *arg;
    if (is_main_func) {
        auto ptr = newNode<Pointer>(NodePtr, var->tok);
        ptr->value = var;
        arg = ptr;
    } else {
        auto ident = newNode<Identifier>(NodeIdent, var->tok);
        ident->var = var;
        arg = ident;
    }
    arg->next = call->args;
    call->args = arg;
}

void Parser::analyzeCalls(Function *fn, bool is_main_func) {
    constexpr NodeId Analyzing = TypeFuncUnresolved;

    if (fn->is_signed || !fn->calls) return;
    fn->id = Analyzing;
    bool is_updated = true;
    for (auto &call : *fn->calls) {
        LOG() << fn->tok->str() << " " << call->fn->tok->str() << '\n';
        if (call->fn->id != Analyzing) analyzeCalls(call->fn, false);
        if (call->fn->isThrowable()) {
            updateCall(fn, call, is_main_func);
        } else if (call->fn->id == Analyzing) {
            unresolvedCalls.push_back({fn, call, is_main_func});
            is_updated = false;
        }
    }
    fn->is_signed = true;   // reuse this field, func has been analyzed.
    if (is_updated) fn->id = TypeFunc;

    if (is_main_func && !unresolvedCalls.empty()) {
        for (auto &[f, call, is_main] : unresolvedCalls) {
            if (call->fn->isThrowable())
                updateCall(f, call, is_main);
            f->id = TypeFunc;
        }
    }
}

void Parser::parseFunctions(Context *ctx) {
    curCtx = ctx;
    for (auto const &val : ctx->decls) {
        Type *ty = (Type *)val.second;
        if (ty->id == TypeFunc && ty->tid == 0) {
            ty->tid = curTypeId++;
            Function *fn = (Function *)ty;
            Context fn_ctx = {};
            fn_ctx.func = fn;
            fn_ctx.parent = ctx;

            // add params and local variables in function context
            auto add_into_fn_ctx = [this, &fn_ctx](Node *node) {
                Node head{};
                Node *cur = &head;
                while (node) {
                    auto var = addVar(&fn_ctx, node->tok, true);
                    var->type = (Type *)node;
                    cur = cur->next = var;
                    node = node->next;
                }
                return head.next;
            };
            fn->params = add_into_fn_ctx(fn->params);
            fn->locals = add_into_fn_ctx(fn->locals);
    
            if (fn->body.token) fn->body.node = parseCodeBlock(fn->body.token, &fn_ctx, true);
            curCtx = ctx;
        } else if (ty->id == NodeContext) {
            parseFunctions((Context* )ty);
            curCtx = ctx;
        }
    }
}

// import = "#import" "[" library-functions* "]"
// library-functions = string ("stdcall" | "cdecl") "[" (word: string spec-block)* "]"
Token * Parser::parseImport(Token *tok, Context *ctx) {
    assert(tok->id == TokenIdMacroImport);

    auto main_tok = tok;
    tok = skip(tok->next, TokenIdLBracket, '[');
    while (tok->id != TokenIdRBracket) {
        auto lib_name = tok;
        tok = skip(tok, TokenIdString, "library name");

        // calling convention
        FuncAttr attr{};
        if (tok->id == TokenIdWord) {
            if (tok->equals("stdcall")) attr = FuncStdcall;
            else if(tok->equals("cdecl")) attr = FuncCdecl;
            else comp.error(tok, err_expected, "calling convention (stdcall or cdecl");
        } else {
            comp.error(tok, err_expected, "calling convention (stdcall or cdecl)");
        }

        // "[" (word: string spec-block)* "]"
        tok = skip(tok->next, TokenIdLBracket, '[');
        while (tok->id != TokenIdRBracket) {
            auto func = newNode<Function>(TypeFunc, tok);
            tok = skip(tok, TokenIdSetWord, "set-word"); 
           
            func->import_name = tok;
            tok = skip(tok, TokenIdString, "string");
            
            func->attrs = static_cast<FuncAttr>(attr | FuncExtern);
            func->data.import_lib = lib_name;
            tok = parseFuncSpec(tok, func, true);
            addDecl(ctx, func->tok, func);
        }
        tok = tok->next;
    }
    main_tok->next = tok->next;
    return tok->next;
}

// * Collect function, import function, alias, context and enum
// * Recognize path! and get-path!
Token * Parser::collectDeclarations(Token *tok, Context *ctx, bool is_block, bool is_func_ctx) {
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
        case TokenIdGetWord: 
            if (tok->next->id == TokenIdSlash) {    // a path
                tok->id = (tok->id == TokenIdWord) ? TokenIdPath : TokenIdGetPath;
            }
            break;
        case TokenIdSetWord:
        case TokenIdSetColon:
            node = collectDecl(tok, &rest);
            if (node) {
                if (cur->id == TokenIdPath || tok->id == TokenIdSetColon)
                    comp.error(tok->next, err_cannot_declared, tok->next->str());
                else if (cur->id == TokenIdSetWord)
                    comp.error(cur, err_invalid_cascade, tok->next->str());
                addDecl(ctx, tok, node);
                skip_decl = true;
            }
            break;
        case TokenIdMacroEnum:
            rest = parseEnum(tok, ctx);
            skip_decl = true;
            break;
        case TokenIdMacroImport:
            rest = parseImport(tok, ctx);
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
                        if (is_func_ctx) return head.next;
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

Unary * Parser::getStructMember(Unary *st, Token *tok) {
    Variable *var = (Variable *)st->value;
    assert(var->type->id == TypeStruct);

    for (auto m = var->type->data.member; m; m = m->next) {
        if (tok->equals(m->tok)) {
            auto mem = newNode<Unary>(NodeMemberAccess, tok);
            mem->value = m;
            st->next = mem;
            return mem;
        }
    }
    return nullptr;
}

Node * Parser::parsePath(Token *tok, Token **rest, bool &is_setpath) {
    Token *next = nullptr;
    Variable *var = nullptr;
    Context *saved_ctx = curCtx;
    is_setpath = false;

    // resolve first word in the path
    do {
        Context *ctx = nullptr;
        var = findVar(tok, &ctx);
        if (var) {
            if (var->id == NodeContext) curCtx = (Context *)var;
            else break;
        } else {
            comp.error(tok, err_undeclared_name, tok->str());
        }

        next = tok->next;
        tok = next->next;
    } while (next->id == TokenIdSlash);

    curCtx = saved_ctx;
    if (!var) comp.error(tok, err_expected, "member in context");

    if (var->id == TypeFunc) return parseFnCall((Function *)var, tok, rest);
    if (var->id != NodeVar) comp.error(tok, err_expected, "variable");

    auto type_id = var->type->id;
    Unary *node = (Unary *)var;
    while (tok->next->id == TokenIdSlash) {
        if (type_id == TypeStruct) {
            node = newNode<Unary>(NodeMemberAccess, tok);
            node->value = var;
            tok = tok->next->next;
            node = getStructMember(node, tok);
            if (!node) comp.error(tok, err_undeclared_name, tok->str());
            type_id = ((Member *)node->value)->id;
        } else if (type_id == TypePtr) {

        } else if (type_id == TypeArray) {

        } else {
            comp.error(tok, err_invalid_path);
        }
    }

    is_setpath = (tok->id == TokenIdSetWord);
    *rest = tok;
    // for case: `a/keyword:` or `a/(n + 1):`
    if (!is_setpath && tok->next->id == TokenIdSetColon) {
        *rest = tok->next;
        is_setpath = true;
    }
    return node;
}

Node * Parser::checkArgType(Node *arg, Type *expected) {
    assert(arg && expected);

    const Type *ty = (const Type *)arg;
    if (arg->id == NodeVar) {
        ty = ((Variable *)arg)->type;
    }
    LOG() << NodeName[ty->id] << " " << NodeName[expected->id] << '\n';
    const auto res = (ty->id == expected->id) ? tc_same : TypeConv[ty->id][expected->id];
    if (res > tc_i_ff)
        comp.error(arg->tok, err_expected_type, TypeName[expected->id], TypeName[ty->id]);
    return arg;
}

Node * Parser::parseFnCall(Function *fn, Token *tok, Token **rest) {
    auto call = newNode<FnCall>(NodeFnCall, tok);
    call->fn = fn;
    Node head{};
    Node *cur = &head;
    if (fn->attrs & FuncVariadic) {
        if (tok->next->id == TokenIdLBracket) {
            tok = tok->next;
            do {
                auto arg = parseExpr(tok->next, &tok);
                cur = cur->next = arg; //checkArgType(arg, (Type *)param);
            } while (tok->next->id != TokenIdRBracket);
            tok = tok->next;
        } else {
            cur->next = parseExpr(tok->next, &tok);
        }
    } else {
        auto param = fn->params;
        if (fn->isThrowable()) param = param->next;
        while (param) {
            auto arg = parseExpr(tok->next, &tok);
            cur = cur->next = arg; //checkArgType(arg, (Type *)param);
            param = param->next;
        }
    }
    call->args = head.next;
    auto caller = curCtx->func;
    if (!caller->calls) caller->calls = new std::vector<FnCall *>;
    caller->calls->push_back(call);

    LOG() << fn->tok->str() << " " << fn->attrs << " " << curCtx->func << " " << curCtx->func->attrs << '\n';

    *rest = tok;
    return call;
}

Node * Parser::parseBinaryOp(Node *lhs, Token *tok, Token **rest) {
    // map TokenId to NodeId
    static const NodeId bin_ops[] = {
        // +        -          /        //           %
        NodeAdd, NodeSub, NodeDiv, NodeModulo, NodeRemainder,
        // *        <          <<            <=          <>
        NodeMul, NodeLess, NodeShiftLeft, NodeLessEq, NodeNotEq,
        // =        >          >=            >>                >>>
        NodeEq, NodeGreater, NodeGreaterEq, NodeShiftRight, NodeShiftRightLogical
    };

    BinaryOp *node = nullptr;
    while (true) {
        auto tok_id = tok->next->id;
        NodeId node_id;
        bool is_bin_op = true;
        if (tok_id > TokenIdBinOpBegin && tok_id < TokenIdBinOpEnd)
            node_id = bin_ops[tok_id - TokenIdBinOpBegin - 1];
        else if (tok_id == TokenIdAnd)
            node_id = NodeAnd;
        else if (tok_id == TokenIdOr)
            node_id = NodeOr;
        else if (tok_id == TokenIdXor)
            node_id = NodeXor;
        //else if (tok_id == TokenIdWord) // TBD: check infix func
        else is_bin_op = false;

        if (is_bin_op) {
            node = newNode<BinaryOp>(node_id, lhs, parseExpr(tok->next->next, &tok));
            lhs = node;
        } else {
            break;
        }
    }
    *rest = tok;
    return node ? node : lhs;
}

Node * Parser::parseExpr(Token *tok, Token **rest) {
    Context *ctx = nullptr;
    Node *node = nullptr;
    Type *ty;
    bool is_set;
    switch (tok->id) {
        case TokenIdWord:
            LOG() << tok->str() << '\n';
            node = findDecl(tok, &ctx);
            if (!node) comp.error(tok, err_undeclared_name, tok->str());
            if (node->id == NodeVar) {
                auto ident = newNode<Identifier>(NodeIdent, tok);
                ident->var = (Variable *)node;
                node = ident;
            } else if (node->id == TypeFunc) {
                node = parseFnCall((Function *)node, tok, &tok);
            }
            break;
        case TokenIdPath:
            node = parsePath(tok, &tok, is_set);
            if (is_set) comp.error(tok, err_invalid_assign);
            break;
        case TokenIdGetWord:
            node = findDecl(tok, &ctx);
            if (node) {
                if (node->id == NodeVar || node->id == TypeFunc) {
                    auto ptr = newNode<Pointer>(NodePtr, tok);
                    ptr->value = node;
                    node = ptr;
                } else {
                    comp.error(tok, err_expected, "variable or function");
                }
            } else {
                comp.error(tok, err_undeclared_name, tok->str());
            }
            break;
        case TokenIdInteger:
        case TokenIdHex:
            ty = newNode<Type>(TypeInt, tok);
            node = ty;
            if (tok->toInteger(ty->data.value))
                comp.error(tok, err_int_overflow);
            break;
        case TokenIdFloat:
            ty = newNode<Type>(TypeDouble, tok);
            node = ty;
            if (tok->toDouble(ty->data.fvalue))
                comp.error(tok, err_float_overflow);
            break;
        case TokenIdString:
            ty = newNode<Type>(TypeCStr, tok);
            node = ty;
            break;
        case TokenIdLParen:
            node = parseExpr(tok->next, &tok);
            tok = tok->next;
            expect(tok, TokenIdRParen, ')');
            break;
        case TokenIdAny:    node = parseAny(tok, &tok); break;
        case TokenIdAll:    node = parseAll(tok, &tok); break;
        case TokenIdEither: node = parseEither(tok, &tok); break;
        case TokenIdCase:   node = parseCase(tok, &tok); break;
        case TokenIdSwitch: node = parseSwitch(tok, &tok); break;
        case TokenIdFunc:  // anonymous function
        case TokenIdAlias:
        default:
            break;
    }

    if (node) node = parseBinaryOp(node, tok, &tok);
    *rest = tok;
    return node;
}

// assignment = lh-side+ expression
Node * Parser::parseAssignment(Token *tok, Token **rest) {
    Context *ctx = nullptr;
    Node *node = nullptr;
    bool is_set = false;
    if (tok->id == TokenIdSetWord) {
        node = findVar(tok, &ctx);
        is_set = true;
    } else if (tok->id == TokenIdPath) {
        node = parsePath(tok, &tok, is_set);
    } else {
        node = parseExpr(tok, rest);
    }

    if (is_set)
        node = newNode<Assignment>(NodeAssign, node, parseAssignment(tok->next, rest));
    return node;
}

void Parser::addTempLocalVar() {
    auto fn = curCtx->func;
    if (!fn->hasAttr(FuncHasTempVar)) {
        fn->addAttr(FuncHasTempVar);
        auto var = newNode<Variable>(NodeVar, &tempVarTok);
        var->type = &boolTy;
        var->next = fn->locals;
        fn->locals = var;
        fn->n_locals++;
    }
}

Node * Parser::parseAnyAll(Token *tok, Token **rest, NodeId id) {
    auto node = newNode<Any>(id, tok);
    addTempLocalVar();
    tok = skip(tok->next, TokenIdLBracket, '[');
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        cur = cur->next = parseExpr(tok, &tok);
        tok = tok->next;
        if (tok->id == TokenIdRBracket) {
            *rest = tok;
            break;
        }
    }
    node->cases = head.next;
    return node;
}

Node * Parser::parseCase(Token *tok, Token **rest) {
    auto node = newNode<Case>(NodeCase, tok);

    tok = skip(tok->next, TokenIdLBracket, '[');
    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdTrue) {   // catch-all rule
            auto true_case = parseCode(tok->next, &tok, curCtx);
            *rest = tok->next;
            expect(*rest, TokenIdRBracket, ']');
            if (node->cases) {
                node->cases->else_body = true_case;
                return node;
            } else {
                return true_case;
            }
        }

        auto case_node = newNode<Either>(NodeEither, tok);
        if (node->cases)
            node->cases->else_body = case_node;
        else
            node->cases = case_node;

        case_node->cond = parseExpr(tok, &tok);

        tok = tok->next;
        expect(tok, TokenIdLBracket, '[');
        case_node->then_body = parseCode(tok, &tok, curCtx);

        tok = tok->next;
        if (tok->id == TokenIdRBracket) {
            *rest = tok;
            break;
        }
    }
    return node;
}

Node * Parser::parseSwitch(Token *tok, Token **rest) {
    auto node = newNode<Switch>(NodeSwitch, tok);
    node->value = parseExpr(tok->next, &tok);
    node->cases = new SwitchCases;

    tok = skip(tok->next, TokenIdLBracket, '[');
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdWord && tok->equals("default")) {
            node->defaultCase = parseCode(tok->next, &tok, curCtx);
            *rest = tok->next;
            expect(*rest, TokenIdRBracket, ']');
            return node;
        }

        auto val = parseExpr(tok, &tok);
        if (val->id != TypeInt && val->id != TypeByte)
            comp.error(tok, err_expected, "integer! or byte! literal value");
        cur = cur->next = val;

        tok = tok->next;
        if (tok->id == TokenIdLBracket) {    // body block for current case
            auto body = parseCode(tok, &tok, curCtx);
            cur = head.next;
            do {
                node->cases->push_back({cur, body});
                cur = cur->next;
            } while (cur);
            cur = &head;
            tok = tok->next;
        }

        if (tok->id == TokenIdRBracket) {
            if (cur != &head) comp.error(tok, err_expected, "body block");
            *rest = tok;
            break;
        }
    }
    if (node->cases->empty()) comp.error(node->tok, err_expected, "case in switch");
    return node;
}

Node * Parser::parseLoop(Token *tok, Token **rest) {
    auto node = newNode<Loop>(NodeLoop, tok);
    node->counter = parseExpr(tok->next, &tok);
    node->body = parseCode(tok->next, rest, curCtx);
    return node;
}

Node * Parser::parseUntil(Token *tok, Token **rest) {
    auto node = newNode<Until>(NodeUntil, tok);
    node->body = parseCode(tok->next, rest, curCtx);
    return node;
}

Node * Parser::parseWhile(Token *tok, Token **rest) {
    auto node = newNode<While>(NodeWhile, tok);
    node->cond = parseCode(tok->next, &tok, curCtx);
    node->body = parseCode(tok->next, rest, curCtx);
    return node;
}

Node * Parser::parseEither(Token *tok, Token **rest) {
    auto node = newNode<Either>(NodeEither, tok);
    node->cond = parseExpr(tok->next, &tok);
    node->then_body = parseCode(tok->next, &tok, curCtx);
    node->else_body = parseCode(tok->next, rest, curCtx);
    return node;
}

// if-stmt = "if" cond-expr body-block
Node * Parser::parseIf(Token *tok, Token **rest) {
    auto node = newNode<If>(NodeIf, tok);
    node->cond = parseExpr(tok->next, &tok);
    node->body = parseCode(tok->next, rest, curCtx);
    return node;
}

Node * Parser::parseReturn(Token *tok, Token **rest) {
    auto node = newNode<Return>(NodeReturn, tok);
    node->expr = parseExpr(tok->next, rest);
    return node;
}

Node * Parser::parseThrow(Token *tok, Token **rest) {
    auto node = newNode<Throw>(NodeThrow, tok);
    node->value = parseExpr(tok->next, rest);
    if (!curCtx->func->isThrowable()) {
        // add a hidden param for exception handling
        auto fn = curCtx->func;
        auto param = newNode<Variable>(NodeVar, &hiddenParamTok);
        param->type = &uintPtrTy;
        param->next = fn->params;
        fn->params = param;
        fn->n_params++;
        fn->addAttr(FuncThrow);
    }
    return node;
}

Node * Parser::parseCatch(Token *tok, Token **rest) {
    auto node = newNode<Catch>(NodeCatch, tok);
    node->filter = parseExpr(tok->next, &tok);
    node->body = parseCode(tok->next, &tok, curCtx);
    *rest = tok;
    if (tok->next->id == TokenIdLBracket) node->filter = parseCode(tok->next, rest, curCtx);
    return node;
}

Node * Parser::parseCode(Token *tok, Token **rest, Context *ctx, bool is_block) {
    if (is_block) tok = skip(tok, TokenIdLBracket, '[');
    curCtx = ctx;
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        Node *node = nullptr;
        switch (tok->id)
        {
        // declaration
        case TokenIdMacroEnum:
        case TokenIdMacroImport: 
            tok = tok->next;
            continue;
        // statement
        case TokenIdIf: node = parseIf(tok, &tok); break;
        case TokenIdEither: node = parseEither(tok, &tok); break;
        case TokenIdCase: node = parseCase(tok, &tok); break;
        case TokenIdSwitch: node = parseSwitch(tok, &tok); break;
        case TokenIdWhile: node = parseWhile(tok, &tok); break;
        case TokenIdUntil: node = parseUntil(tok, &tok); break;
        case TokenIdLoop: node = parseLoop(tok, &tok); break;
        case TokenIdBreak: node = newNode<Node>(NodeBreak, tok); break;
        case TokenIdContinue: node = newNode<Node>(NodeContinue, tok); break;
        case TokenIdReturn: node = parseReturn(tok, &tok); break;
        case TokenIdThrow: node = parseThrow(tok, &tok); break;
        case TokenIdCatch: node = parseCatch(tok, &tok); break;

        case TokenIdExit:
        case TokenIdWith:
        case TokenIdUse: tok = tok->next; break;
        case TokenIdRBracket:
            if (is_block) {
                *rest = tok;
                return head.next;
            }
        case TokenIdSetWord:
            switch (tok->next->id)
            {
            case TokenIdContext:
                node = parseCode(tok->next->next, &tok, findContext(tok));
                curCtx = ctx;
                break;
            case TokenIdFunc:   // already handled in collect phase
            case TokenIdAlias:
                tok = tok->next->next;
                continue;
            default:
                break;
            }
        // assignment or expression
        default:
            node = parseAssignment(tok, &tok);
            break;
        }
        tok = tok->next;
        if (node)
            cur = cur->next = node;
    }
    *rest = tok;
    return head.next;
}

Node * Parser::parseCodeBlock(Token *tok, Context *ctx, bool is_func_body) {
    (LOG() << "start\n").indent();

    curCtx = ctx;
    tok = collectDeclarations(tok, ctx, is_func_body, is_func_body);

    dumpTokens(tok, "After collectDeclarations");

    // resolve struct and function spec
    resolveDeclarations(ctx);

    parseFunctions(ctx);

    LOG() << "parseCode start\n";
    Node *ast = parseCode(tok, &tok, ctx, is_func_body);
    LOG() << "parseCode end\n";

    dumpAst(ast);

    curCtx = ctx;

    addTypeInfo(ast);

    LOG.unindent(); LOG() << "end\n";
    return ast;
}

Context * Parser::parse(Token *tok) {
    Context *ctx = newContext(tok);
    Function *mainFunc = newNode<Function>(TypeFunc, tok);
    ctx->is_root = true;
    ctx->func = mainFunc;
    mainFunc->body.node = parseCodeBlock(tok, ctx);
    analyzeCalls(mainFunc);
    return ctx;
}

template <typename... Args>
void Parser::error(const Node *node, unsigned diag_id, Args &&... args) {
    comp.error(node->tok, diag_id, std::forward<Args>(args)...);
}

// For debugging
void dumpFunction(Function *func) {if constexpr (verbose >= 4) {
    llvm::errs() << func->tok->str() << ":\n";
    llvm::errs() << "Params: " << func->n_params << " ";
    auto p = func->params;
    while (p) {
        llvm::errs() << p->tok->str() << ": " << TypeName[p->id] << " ";
        p = p->next;
    }
    llvm::errs() << "\nLocals: " << func->n_locals << " ";
    p = func->locals;
    while (p) {
        llvm::errs() << p->tok->str() << ": " << TypeName[p->id] << " ";
        p = p->next;
    }
    llvm::errs() << '\n';
}}

static void dumpBinOp(Node *node, DumpState &s) {if constexpr (verbose >= 4) {
    BinaryOp *op = (BinaryOp *)node;
    dumpNode(op->lhs, s);
    llvm::errs() << NodeName[op->id] << " ";
    dumpNode(op->rhs, s);
}}

static void dumpAssign(Node *node, DumpState &s) {if constexpr (verbose >= 4) {
    BinaryOp *op = (BinaryOp *)node;
    dumpNode(op->lhs, s);
    dumpNode(op->rhs, s);
}}

void dumpNode(Node *node, DumpState &s) {if constexpr (verbose >= 4) {
    using llvm::errs;
    auto i = s.indent;
    auto indent = [i]{
        uint32_t n = i; 
        while (n--) errs() << "  ";
    };
    // errs() << NodeName[node->id] << "--";
    s.nNode++;
    switch (node->id)
    {
    case NodeAssign: 
        s.is_bin_op = true;
        dumpAssign(node, s);
        s.is_bin_op = false;
        break;
    case NodeIf: 
        indent();
        errs() << "If Statement:\n";
        errs() << "Conditional: ";
        dumpAst(((If *)node)->cond, s);
        errs() << "\nBody: ";
        dumpAst(((If *)node)->body, s);
        errs() << '\n';
        break;
    case NodeReturn:
        errs() << node->tok->str() << " ";
        dumpAst(((Return *)node)->expr, s);
        errs() << '\n';
        break;
    case TypeInt: 
        errs() << node->tok->str() << " ";
        break;

    case NodeAdd: case NodeSub: case NodeDiv: case NodeMul: case NodeModulo: case NodeRemainder:
    case NodeLessEq: case NodeLess: case NodeEq: case NodeGreaterEq: case NodeGreater: case NodeNotEq:
    case NodeShiftLeft: case NodeShiftRight: case NodeShiftRightLogical: case NodeAnd: case NodeOr: case NodeXor:
        s.is_bin_op = true;
        dumpBinOp(node, s);
        s.is_bin_op = false;
        break;
    case NodeIdent:
        errs() << node->tok->str() << " ";
        break;
    case NodeVar:
        errs() << node->tok->str() << " ";
        break;
    case NodeFnCall:
        errs() << "call: " << node->tok->str() << "( ";
        dumpAst(((FnCall *)node)->args, s);
        errs() << ") ";
        if (!s.is_bin_op) errs() << '\n';
        break;
    default:
        break;
    }
}}

void dumpAst(Node *node, DumpState s) {if constexpr (verbose >= 4) {
    using llvm::errs;

    if (s.indent == 0) errs() << "+++++++++++dump AST+++++++++++" << "\n";
    uint32_t n = 0;
    s.indent++;
    while (node) {
        n++;
        dumpNode(node, s);
        node = node->next;
    }
    s.indent--;
    if (s.indent == 0) errs() << "\n+++++Number of AST Node+++++: " << n << " " << s.nNode << '\n';
}}
