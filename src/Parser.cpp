/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include <cfloat>
#include <limits>
#include "LexMap.h"
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
static const TypeConvResult TypeConv[TypeStruct + 1][TypeStruct + 1] = {
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

Variable * Parser::addVar(Context *ctx, Token *name, Variable *var, bool is_local) {
    if (!var) var = newNode<Variable>(NodeVar, name);
    auto& vars = ctx->decls;
    auto const & [keyval, is_new] = vars.insert({name->str(), var});
    if (!is_new) {
        if (is_local) comp.error(name, err_symbol_declared, name->str());
        else var = (Variable *)keyval->second;
    } else if (is_new && !is_local) {
        var->addAttr(VarGlobal);
    }
    return var;
}

void Parser::addDecl(Context *ctx, Token *name, Node *node) {
    auto& decls = ctx->decls;
    auto const & [keyval, is_new] = decls.insert({name->str(), node});
    if (!is_new)
        comp.error(name, err_symbol_declared, name->str());
}

static Node * _findDecl(Token *name, Context *ctx, Context **ctx_out, bool bubble) {
    do {
        auto& decls = ctx->decls;
        auto it = decls.find(name->str());
        if (it != decls.end()) {
            *ctx_out = ctx;
            return it->second;
        } else {
            if (bubble) ctx = ctx->parent;
            else return nullptr;
        }
    } while (ctx);
    return nullptr;
}

Node * Parser::findDecl(Token *name, Context **ctx_out, Context *ctx) {
    LOG() << name->str() << ' ' << ctx << ' ' << withNS.empty() << '\n';
    if (ctx) return _findDecl(name, ctx, ctx_out, false);
    if (withNS.empty()) {
        return _findDecl(name, curCtx, ctx_out, true);
    } else {
        for (auto &ctx : withNS) {
            LOG() << name->str() << ' ' << ctx->tok->str() << '\n';
            if (auto v = _findDecl(name, ctx, ctx_out, false))
                return v;
        }
    }
    return nullptr;
}

Variable * Parser::findVar(Token *name, Context **ctx_out, Context *ctx) {
    return (Variable *)findDecl(name, ctx_out, ctx);
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
        {"byte!", Type{TypeByte, false, 1, 1, 0, nullptr, nullptr, TypeByte}},
        {"c-string!", Type{TypeCStr, false, 8, 8, 0, nullptr, nullptr, TypePtr}},
        {"float!", Type{TypeDouble, false, 8, 8, 0, nullptr, nullptr, TypeDouble}},
        {"float32!", Type{TypeFloat, false, 4, 4, 0, nullptr, nullptr, TypeFloat}},
        {"integer!", Type{TypeInt, true, 4, 4, 0, nullptr, nullptr, TypeInt}},
        {"logic!", Type{TypeBool, false, 1, 1, 0, nullptr, nullptr, TypeBool}},
    }};
    return datatypes;
}

const Type * Parser::findType(Token *name, Context **ctx) {
    static constexpr auto basic_types = genDatatypes();
    auto it = std::lower_bound(basic_types.begin(), basic_types.end(), name->str(),
        [](const auto &a, const auto &b) {return -1 == a.first.compare_insensitive(b);});

    if (it == basic_types.end() || !name->equals(it->first)) {
        auto node = findDecl(name, ctx);
        if (node && node->id < TypeEnd) return (Type *)node;
    } else {
        return const_cast<Type *>(&it->second);
    }
    return nullptr;
}

const Type * Parser::parseType(Token *tok, Token **rest, bool is_block, bool is_collect, bool error) {
    if (is_block) tok = skip(tok, TokenIdLBracket, '[');
    if (tok->id != TokenIdWord) {
        if (error) comp.error(tok, err_expected, "datatype");
        else return nullptr;
    }
    Context *ctx = nullptr;
    const Type *type = findType(tok, &ctx);
    LOG() << type << " " << tok->str() << '\n';
    if (type) {  // alias type
        tok = tok->next;
        if (!is_collect && tok->id == TokenIdWord && tok->equals("value")) { // struct value
            resolveStruct((Type *)type, ctx);
            tok = tok->next;
        } else if ((type->id == TypeStruct || type->id == TypeStructUnresolved) && !isSizeOf) {
            type = newPtrType(type);
        }
    } else {     // anonymous type
        if (tok->equals("struct!")) {
            type = parseStruct(tok, &tok, is_collect);
        } else if (tok->equals("function!")) {
            auto fn = newNode<FuncType>(TypeFuncTy, tok);
            LOG() << fn << '\n';
            tok = parseFuncSpec(tok->next, fn, nullptr, is_collect);
            type = fn;
        } else if (tok->equals("pointer!")) {
            type = newPtrType(parseType(tok->next, &tok, true, is_collect));
        } else {
            if (error) comp.error(tok, err_invalid_type);
            else return nullptr;
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
        if (tok->id == TokenIdWord || tok->sub_id == TokenIdKeyword) {
            t = t->next = tok;
            tok = tok->next;
            n++;
        } else if (tok->id == TokenIdLBracket && n > 0) {
            total += n;
            auto name = ident.next;
            auto type = parseType(tok, &tok);
            while (n) {
                Variable *local = newNode<Variable>(NodeVar, name);
                local->type = type;
                cur = cur->next = local;
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
            Variable *local = newNode<Variable>(NodeVar, name);
            cur = cur->next = local;
            name = name->next;
        }
    }
    func->locals = head.next;
    return tok;
}

// func-spec-block = "[" func-attrs-block? doc-string? param* return-spec? ("/local" local-var+)? "]"
Token *Parser::parseFuncSpec(Token *tok, FuncType *ftype, Function *func, bool is_collect) {
    if (!ftype) ftype = func->type;
    if (is_collect) {
        if (func) func->id = TypeFuncUnresolved;
        ftype->spec = tok;
        skipBlock(tok, &tok);
        return tok;
    }

    if (func) func->id = TypeFunc;
    ftype->spec = nullptr;
    ftype->return_type = &voidTy;
    tok = skip(tok, TokenIdLBracket, '[');
    if (consume(tok, TokenIdRBracket, &tok))
        return tok;

    // attribute block
    if (tok->id == TokenIdLBracket) {
        auto attrs = ftype->attrs;
        do {
            using namespace llvm;
            tok = tok->next;
            if (tok->equals("variadic"))     attrs |= FuncVariadic;
            else if (tok->equals("typed"))   attrs = attrs | FuncVariadic | FuncTyped;
            else if (tok->equals("cdecl"))   attrs = attrs & (~FuncCallConv) | FuncCdecl;
            else if (tok->equals("stdcall")) attrs = attrs & (~FuncCallConv) | FuncStdcall;
            else if (tok->id == TokenIdRBracket) break;
            else comp.error(tok, err_invalid_func);
        } while (tok->id != TokenIdEof);
        ftype->attrs = attrs;
        tok = tok->next;
    }

    // doc string?
    consume(tok, TokenIdString, &tok);

    // param* return-spec? ("/local" local-var+)?
    enum State {SParam, SReturn, SLocal};
    State state = SParam;
    Node params{};
    ftype->params = &params;
    while (tok->id != TokenIdRBracket) {
        if (state == SParam && tok->id == TokenIdWord) {
            // param = word "[" type "]" doc-string?
            auto param = newNode<Variable>(NodeVar, tok);
            LOG() << tok->str() << " " << param << '\n';
            param->type = parseType(tok->next, &tok);
            LOG() << (int)param->type->id << " " << param->type << " " << TypeName[param->type->id] << '\n';
            ftype->params = ftype->params->next = param;
            ftype->n_params++;
            consume(tok, TokenIdString, &tok);

        } else if (state < SReturn && tok->id == TokenIdReturn && tok->next->id == TokenIdSetColon) {
            // return-spec? = "[" type "]" doc-string?
            state = SReturn;
            ftype->return_type = parseType(tok->next->next, &tok);
            consume(tok, TokenIdString, &tok);

        } else if (state < SLocal && tok->id == TokenIdRefinement && tok->equals("local") && func) {
            //local-var = word+ ("[" type "]")?
            state = SLocal;
            tok = parseLocal(tok->next, func);
        } else {
            comp.error(tok, err_invalid_func);
        }
    }
    ftype->params = params.next;

    return tok->next;
}

void Parser::parseMembers(Type *st, Token *tok, Token **rest) {
    tok = skip(tok, TokenIdLBracket, '[');

    if (tok->id == TokenIdLBracket) {   // attributes
        do {
            tok = tok->next;
            if (tok->equals("packed")) st->is_signed = true;
            else if (tok->id == TokenIdRBracket) break;
            else comp.error(tok, err_invalid_struct);
        } while (tok->id != TokenIdEof);
        tok = tok->next;
    }

    Node head{};
    Node *cur = &head;
    uint32_t idx = 0;
    while (tok->id != TokenIdEof) {
        // typed-word = word "[" type "]"
        if (tok->id == TokenIdWord || tok->sub_id == TokenIdKeyword) {
            Member *member = newNode<Member>(NodeMember, tok);
            member->type = parseType(tok->next, &tok);;
            member->idx = idx++;
            cur = cur->next = member;
        } else if (tok->id == TokenIdRBracket) {
            break;
        } else {
            comp.error(tok, err_invalid_struct, tok->str());
        }
    }
    if (!idx) comp.error(tok, err_invalid_struct, tok->str());  // empty struct
    *rest = tok->next;
    st->data.member = head.next;
}

Type * Parser::parseStruct(Token *tok, Token **rest, bool is_collect) {
    assert(tok->equals("struct!"));
    // struct-type = struct! "[" typed-word+ "]" "value"?
    Type *st = newNode<Struct>(TypeStruct, tok);
    tok = tok->next;
    if (is_collect) {
        st->id = TypeStructUnresolved;
        st->data.st_body = tok;
        skipBlock(tok, rest);
        return st;
    }

    parseMembers(st, tok, &tok);
    *rest = tok;
    if (tok->id == TokenIdWord && tok->equals("value")) {
        *rest = tok->next;  // skip token "value"
        return st;
    } else {
        return newPtrType(st);
    }
}

// declaration = qualified-word: (function-decl | context-decl | alias-decl)
Node * Parser::collectDecl(Token *tok, Token **rest, bool is_func_ctx) {
    assert(tok->id == TokenIdSetWord || tok->id == TokenIdSetColon);

    *rest = tok;
    Token *name = tok;
    tok = tok->next;

    // function-decl = ("func" | "function") func-spec-block body-block
    if (tok->id == TokenIdFunc) {
        auto func = newFunction(name);
        func->import_name = name;
        // func-spec-block
        tok = parseFuncSpec(tok->next, nullptr, func, true);
LOG() << tok << '\n';
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
    Type *node = newNode<Type>(TypeInt, tok);
    node->tid = curTypeId++;
    addDecl(ctx, tok, node);

    tok = skip(tok, TokenIdWord, "word");
    tok = skip(tok, TokenIdLBracket, '[');

    int64_t val = 0;
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdWord && head.next == nullptr) {
            Type *elem = newNode<Type>(TypeInt, tok);
            elem->next = node;
            elem->tid = node->tid;
            elem->data.value = val++;
            addDecl(ctx, tok, elem);
        } else if (tok->id == TokenIdSetWord) {
            Type *elem = newNode<Type>(TypeInt, tok);
            cur = cur->next = elem;
        } else if ((tok->id == TokenIdHex || tok->id == TokenIdInteger || tok->id == TokenIdWord) && head.next) {
            if (tok->id == TokenIdWord) {
                Context *ctx_out = nullptr;
                Type *v = (Type *)findDecl(tok, &ctx_out);
                if (v->id == TypeInt) val = v->data.value;
                else comp.error(tok, err_invalid_enum, tok->str());
            } else {
                tok->toInteger(val);
            }
            cur = head.next;
            do {
                Type *elem = (Type *)cur;
                cur = cur->next;
                elem->next = node;
                elem->tid = node->tid;
                elem->data.value = val;
                addDecl(ctx, elem->tok, elem);
            } while (cur);
            val++;
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
        parseMembers(ty, ty->data.st_body, &rest);
        ty->id = TypeStruct;
        if (ty->tid) tids.pop_back();
        curCtx = savedCtx;
        return true;
    }
    return false;
}

void Parser::resolveFuncSpec(Type *ty) {
    Token *spec = nullptr;
    FuncType *ftype = nullptr;
    Function *fn = nullptr;
    if (ty->id == TypeFuncUnresolved) {
        fn = (Function *)ty;
        spec = fn->type->spec;
    } else if (ty->id == TypeFuncTy) {
        ftype = (FuncType *)ty;
        spec = ftype->spec;
    }
    if (spec) {
        parseFuncSpec(spec, ftype, fn);
        if (fn) dumpFunction(fn);
    }
}

void Parser::resolveDeclarations(Context *ctx) {
    curCtx = ctx;
    for (auto const &val : ctx->decls) {
        Type *ty = (Type *)val.second;
        if (!resolveStruct(ty, ctx) && ty->id == NodeContext) {
            resolveDeclarations((Context *)ty);
            curCtx = ctx;
        } else {
            // if (ty->id == TypePtr) ;
            resolveFuncSpec(ty);
        }
    }
}

const Type * Parser::addVarType(Node *node) {
    if (node->id == NodeAssign) {
        Assignment *assign = (Assignment *)node;
        auto dest_id = assign->lhs->id;
        const Type *ty = addVarType(assign->rhs);
        if (dest_id == NodeVar) {
            Variable *var = (Variable *) assign->lhs;
            auto rhs = assign->rhs;
            if (rhs->id == NodeDeclare) {
                var->addAttr(VarDeclare);
                rhs->tok = var->tok;
                rhs->is_signed = var->hasAttr(VarGlobal);
            }
            if (!var->type) {
                if (ty->id == TypeVoid) error(assign->rhs, err_no_type, var->tok->str());
                var->type = ty;
            } else {
                // Type checking
                if (var->type->id == TypePtr && ty->id == TypeNull) return ty;
                if (!var->isType(ty))
                    error(assign->rhs, err_expected_type, TypeName[var->type->id], TypeName[ty->id]);
            }
            if (var->hasAttr(VarGlobal) && ty->id == TypeCStr) var->size = ty->tok->len + 1;
            if (var->hasAttr(VarGlobal) && assign->rhs->id == NodeLiteralArray) {
                LitArray *arr = (LitArray *)assign->rhs;
                var->size = arr->num;
            }
        } else if (dest_id == NodeMemberAccess) {

        }
        assign->type = ty;
        return ty;
    } else {
        return analyzeNodeType(node);
    }
}

const Type * Parser::analyzeMathType(BinaryOp *bin) {
    auto rhs = bin->rhs;
    auto lhs_ty = analyzeNodeType(bin->lhs);
    auto rhs_ty = analyzeNodeType(rhs);
    if (lhs_ty->id == TypeByte || lhs_ty->id == TypePtr || lhs_ty->id == TypeCStr) {
        if (rhs_ty->id != TypeInt && rhs_ty->id != lhs_ty->id)
            error(rhs, err_conv_type, TypeName[rhs_ty->id], TypeName[TypePtr]);
    } else {
        auto result = TypeConv[rhs_ty->id][lhs_ty->id];
        auto msg_id = note_no_error;
        switch (result)
        {
        case tc_same: break;
        case tc_i_ii: 
        case tc_i_if:
        case tc_i_ff: bin->rhs = newCast(rhs, lhs_ty); break;
        case tc_e_ff: 
            if (rhs->id < TypeType) {
                Type *ty = (Type *)rhs;
                if (ty->data.fvalue <= FLT_MAX) ty->id = TypeFloat;
            } break;
        case tc_fail: msg_id = err_conv_type; break;
        default: msg_id = err_no_implicit_conv; break;
        }
        if (msg_id != note_no_error)
            error(bin, msg_id, TypeName[rhs_ty->id], TypeName[lhs_ty->id]);
    }
    bin->type = lhs_ty;
    return lhs_ty;
}

const Type * Parser::getPathType(Path *path) {
    Variable *var = (Variable *)path->first;
    const Type *base_ty = var->type;

    Unary *node = (Unary *)path->value;
    while (node) {
        auto type_id = node->id;
        if (type_id == NodeMemberAccess) {
            Member *m = (Member *)node->value;
            base_ty = m->type;
        } else if (type_id == NodeIdent || type_id == TypeInt) {
            if (base_ty->id == TypeCStr) base_ty = &byteTy;
            else if (base_ty->id == TypePtr) base_ty = base_ty->data.type;
            else error(node, err_invalid_path);
        }
        node = (Unary *)node->next;
    };
    return base_ty;
}

const Type * Parser::analyzeNodeType(Node *node) {
    LOG() << NodeName[node->id] << '\n';

    if (node->id < TypeEnd) return (Type *)node;

    auto add_local_var = [this](const Type *ty, Node *node) {
        auto var = newNode<Variable>(NodeVar, node->tok);
        var->type = ty;
        var->next = curFunc->locals;
        curFunc->locals = var;
        return var;
    };

    switch (node->id)
    {
    case NodeAssign: {
        Assignment *assign = (Assignment *)node;
        if (assign->type) return assign->type;
        else return addVarType(node);
    }
    case NodeFnCall:
        analyzeType(((FnCall *)node)->args);
        return ((FnCall*)node)->type->return_type;

    case NodeVar: return ((Variable*)node)->type;
    case NodeCast: {
        analyzeNodeType(((Cast *)node)->expr);
        return (((Cast *)node)->type);
    }
    case NodeAdd:
    case NodeSub:
    case NodeMul:
    case NodeDiv:
    case NodeModulo:
    case NodeRemainder:  return analyzeMathType((BinaryOp *)node);
    case NodeEq:        
    case NodeNotEq:     
    case NodeLess:      
    case NodeLessEq:    
    case NodeGreater:   
    case NodeGreaterEq: {
        BinaryOp *bin = (BinaryOp *)node;
        auto t1 = analyzeNodeType(bin->lhs);
        auto t2 = analyzeNodeType(bin->rhs);
        bin->type = t1;
        if (t1->id != TypeVoid && t2->id != TypeVoid && 
            (t1->id == t2->id || t1->id == TypeNull || t2->id == TypeNull))
            return &boolTy;
        else error(bin, err_invalid_type);
    }
    case NodeAnd:       
    case NodeOr:        
    case NodeXor:       
    case NodeShiftLeft: 
    case NodeShiftRight:
    case NodeShiftRightLogical: {
        BinaryOp *bin = (BinaryOp *)node;
        bin->type = analyzeNodeType(bin->lhs);
        return bin->type;
    }
    case NodeEither: {
        Either *e = (Either *)node;
        analyzeNodeType(e->cond);
        auto t1 = analyzeType(e->then_body);
        auto t2 = t1;
        if (e->else_body) t2 = analyzeType(e->else_body);
        if (t1->id != TypeVoid && t2->id != TypeVoid && 
            (t1->id == t2->id || t1->id == TypeNull || t2->id == TypeNull)) {
            e->result = add_local_var(t1, node);
            return t1;
        } else {
            return &voidTy;
        }
    }
    case NodeSwitch: {
        Switch *s = (Switch *)node;
        const Type *val_ty = analyzeNodeType(s->value);

        for (auto &s : *s->cases) {
            //auto result = TypeConv[val->id][val_ty->id];
            if (s.first->id != val_ty->id) {
                s.first->id = val_ty->id;
            }
        }

        const Type *ty = nullptr;
        for (auto &[val, body] : *s->cases) {
            auto cur_ty = analyzeType(body);
            if (!ty) ty = cur_ty;
            else if (cur_ty->id != ty->id) return &voidTy;
        }
        if (s->defaultCase) {
            auto cur_ty = analyzeType(s->defaultCase);
            if (!ty) ty = cur_ty;
            else if (cur_ty->id != ty->id) return &voidTy;
        }
        if (ty->id != TypeVoid) s->result = add_local_var(ty, node);
        return ty;
    }
    case NodeWhile: {
        While *w = (While *)node;
        analyzeType(w->cond);
        analyzeType(w->body);
        return &voidTy;
    }
    case NodeUntil:
        analyzeType(((Until *)node)->body);
        return &voidTy;

    case NodeAny:    // fall thru
    case NodeAll:    analyzeType(((Any *)node)->cases); return &boolTy;
    case NodeCase:   return analyzeNodeType(((Case *)node)->cases);
    case NodeIf:
        analyzeNodeType(((If *)node)->cond);
        return analyzeType(((If *)node)->body);

    case NodeMemberAccess: return getPathType((Path *)node);
    case NodePtrIdx: {
        Variable *var = (Variable *)((Path *)node)->first;
        if (var->type->id == TypeCStr) return &byteTy;
        else return var->type->data.type;
    }
    case NodeDeclare:
        return ((TypedNode *)node)->type;

    case NodeNot:    return analyzeNodeType(((Unary *)node)->value);
    case NodeReturn: {
        auto ret = (Return*)node;
        auto ret_ty = curFunc->type->return_type;
        ret->expr = checkArgType(ret->expr, ret_ty);
        return ret_ty;
    }
    case NodeSizeOf: return &uintTy;
    case NodeNull:   return &nullTy;
    case NodeIdent:  return ((Identifier *)node)->var->type;
    case NodePtr:    return ((Pointer *)node)->type;
    case NodeArray:  return newPtrType(((Array *)node)->type);
    case NodeLiteralArray: {
        auto arr = (LitArray *)node;
        if (arr->is_homogenous) {
            return newPtrType((const Type *)arr->vals);
        } else {
            return &uintPtrTy;
        }
    }
    default:         return &voidTy;
    }
}

const Type * Parser::analyzeType(Node *node) {
    const Type *curTy = &voidTy;
    while (node) {
        curTy = analyzeNodeType(node);
        node = node->next;
    }
    return curTy;
}

void Parser::updateCall(Function *fn, FnCall *call) {
    Variable *var;
    if (fn->isThrowable()) {
        var = (Variable *)fn->type->params;
    } else {
        var = newNode<Variable>(NodeVar, &hiddenParamTok);
        auto ftype = fn->type;
        var->type = &uintPtrTy;
        // add a hidden param in caller for exception handling
        var->next = ftype->params;
        ftype->params = var;
        fn->addAttr(FuncThrow);
    }
    Node *arg;

    // add an argument
    auto ident = newNode<Identifier>(NodeIdent, var->tok);
    ident->var = var;
    arg = ident;
    arg->next = call->args;
    call->args = arg;
}

void Parser::analyzeCalls(Function *fn) {
    constexpr NodeId Analyzing = TypeFuncUnresolved;

    if (fn->is_signed || !fn->calls) return;
    fn->id = Analyzing;
    bool is_updated = true;
    for (auto &call : *fn->calls) {
        LOG() << fn->tok->str() << " " << call->fn->tok->str() << '\n';
        if (call->fn->id != Analyzing) analyzeCalls(call->fn);
        if (call->fn->isThrowable()) {
            updateCall(fn, call);
        } else if (call->fn->id == Analyzing) {
            unresolvedCalls.push_back({fn, call});
            is_updated = false;
        }
    }
    fn->is_signed = true;   // reuse this field, func has been analyzed.
    if (is_updated) fn->id = TypeFunc;

    if (!unresolvedCalls.empty()) {
        for (auto &[f, call] : unresolvedCalls) {
            if (call->fn->isThrowable())
                updateCall(f, call);
            f->id = TypeFunc;
        }
    }
}

void Parser::parseFunc(Function *fn, Context *ctx) {
    if (fn->id == TypeFunc && fn->type->tid == 0) {
        fn->type->tid = curTypeId++;
        Context fn_ctx = {};
        fn_ctx.func = fn;
        fn_ctx.parent = ctx;

        // add params and local variables in function context
        auto add_into_fn_ctx = [this, &fn_ctx](Node *node) {
            while (node) {
                addVar(&fn_ctx, node->tok, (Variable *)node, true);
                node = node->next;
            }
        };
        add_into_fn_ctx(fn->type->params);
        add_into_fn_ctx(fn->locals);
        if (fn->body.token) fn->body.node = parseCodeBlock(fn->body.token, &fn_ctx, true);
        curCtx = ctx;
    } else if (fn->id == NodeContext) {
        parseFunctions((Context* )fn);
        curCtx = ctx;
    }
}

void Parser::parseFunctions(Context *ctx) {
    curCtx = ctx;
    for (auto const &val : ctx->decls) {
        Function *fn = (Function *)val.second;
        parseFunc(fn, ctx);
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
            auto func = newFunction(tok);
            tok = skip(tok, TokenIdSetWord, "set-word"); 
           
            func->import_name = tok;
            tok = skip(tok, TokenIdString, "string");
            
            func->type->attrs = static_cast<FuncAttr>(attr | FuncExtern);
            func->import_lib = lib_name;
            tok = parseFuncSpec(tok, nullptr, func, true);
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
            node = collectDecl(tok, &rest, is_func_ctx);
            if (node) {
                if (cur->id == TokenIdPath || tok->id == TokenIdSetColon)
                    comp.error(tok->next, err_cannot_declared, tok->next->str());
                else if (cur->id == TokenIdSetWord)
                    comp.error(cur, err_invalid_cascade, tok->next->str());
                addDecl(ctx, tok, node);
                if (node->id != NodeContext && !withStack.empty()) {
                    auto with = withStack.back();
                    with->decls.push_back(node);
                }
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
        case TokenIdWith: {
            auto with_tok = tok;
            auto with = new WithDecl();
            with->ctx = ctx;
            withDecls.push_back(with);
            withStack.push_back(with);
            tok = tok->next;
            if (tok->id == TokenIdWord) {
                with->ns.push_back(tok);
            } else if (tok->id == TokenIdLBracket) {
                tok = tok->next;
                while (tok->id != TokenIdRBracket) {
                    if (tok->id != TokenIdWord) comp.error(tok, err_undeclared_ns, tok->str());
                    with->ns.push_back(tok);
                    tok = tok->next;
                }
            }
            for (auto it = withStack.rbegin(); it != withStack.rend(); ++it) {
                with->ns.append((*it)->ns);
            }
            rest = collectDeclarations(tok->next, ctx, true, is_func_ctx);
            withStack.pop_back();
            tok = with_tok;
            skip_decl = true;
        } break;

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

Unary * Parser::getStructMember(const Type *st , Token *tok) {
    for (auto m = st->data.member; m; m = m->next) {
        if (tok->equals(m->tok)) {
            auto mem = newNode<Unary>(NodeMemberAccess, tok);
            mem->value = m;
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
    bool is_root = false;

    // resolve first word in the path
    Context *ctx = nullptr;
    do {
        Context *ctx_out = nullptr;
        var = findVar(tok, &ctx_out, ctx);
        if (var) {
            if (var->id == NodeContext) {
                ctx = (Context *)var;
            } else {
                is_root = ctx_out->is_root;
                break;
            }
        } else {
            comp.error(tok, err_undeclared_name, tok->str());
        }

        next = tok->next;
        tok = next->next;
    } while (next->id == TokenIdSlash);

    curCtx = saved_ctx;
    if (!var) comp.error(tok, err_expected, "member in context");

    if (var->id == TypeFunc) return parseFnCall(nullptr, (Function *)var, tok, rest);
    if (var->id != NodeVar) comp.error(tok, err_expected, "variable");

    const Type *ty = var->type;
    Path *path = nullptr;
    auto type_id = ty->id;
    Node head{};
    Node *cur = &head;
    LOG() << tok->str() << " " << TypeName[type_id] << '\n';

    // special case for system/*
    if (is_root && tok->equals("system")) {
        auto t = tok->next;
        if (t->id == TokenIdSlash && t->next->equals("words")) {
            t = t->next->next;
            if (t->id == TokenIdSlash && t->next->id == TokenIdWord || t->next->id == TokenIdSetWord) {
                tok = t->next;
                Context *ctx_out = nullptr;
                if (var = (Variable *)_findDecl(tok, globalCtx, &ctx_out, false))
                    goto label_end;
                else
                    comp.error(t, err_undeclared_name, t->str());
            }
        }
    }

    while (tok->next->id == TokenIdSlash) {
        if (type_id == TypePtr && ty->data.type->id == TypeStruct) {
            type_id = TypeStruct;
            ty = ty->data.type;
        }
        if (type_id == TypeStruct) {
            if (!path) {
                path = newNode<Path>(NodeMemberAccess, tok);
                path->first = var;
            }
            tok = tok->next->next;
            cur = cur->next = getStructMember(ty, tok);
            if (!cur) comp.error(tok, err_undeclared_name, tok->str());
            Unary *node = (Unary *)cur;
            ty = ((Member *)node->value)->type;
            type_id = ty->id;
        } else if (type_id == TypeCStr || type_id == TypePtr || type_id == NodeArray || type_id == NodeLiteralArray) {
            if (!path) {
                path = newNode<Path>(NodePtrIdx, tok);
                path->first = var;
            }
            tok = tok->next->next;
            if (tok->id == TokenIdInteger) {
                 cur = cur->next = parseInteger(tok);
            } else if (tok->id == TokenIdWord || tok->id == TokenIdSetWord) {
                if (tok->equals("value")) {
                    Type *idx = newNode<Type>(TypeInt, tok);
                    idx->data.value = 1;
                    cur = cur->next = idx;
                } else {
                    Context *ctx = nullptr;
                    auto v = findVar(tok, &ctx);
                    if (v->id == NodeVar) {
                        auto ident = newNode<Identifier>(NodeIdent, tok);
                        ident->var = v;
                        cur = cur->next = ident;
                    } else if (v->id == TypeInt) {  // enum
                        Type *idx = newNode<Type>(TypeInt, tok);
                        idx->data.value = ((Type *)v)->data.value;
                        cur = cur->next = idx;
                    } else {
                        comp.error(tok, err_str_index);
                    }
                }
            } else {
                comp.error(tok, err_str_index);
            }

            // next element's type
            if (type_id == TypeCStr || type_id == NodeArray) {
                type_id = TypeByte;
            } else {
                ty = ty->data.type;
                type_id = ty->id;
            }
        } else {
            comp.error(tok, err_invalid_path);
        }
    }

label_end:
    is_setpath = (tok->id == TokenIdSetWord);
    *rest = tok;
    // for case: `a/keyword:` or `a/(n + 1):`
    if (!is_setpath && tok->next->id == TokenIdSetColon) {
        *rest = tok->next;
        is_setpath = true;
    }

    if (path) {
        path->value = head.next;
        return path;
    } else {
        return var;
    }
}

Node * Parser::checkArgType(Node *arg, const Type *lhs_ty) {
    auto rhs_ty = analyzeNodeType(arg);
    if (lhs_ty->id == TypeStruct) {
        arg->flags |= NodeFlagStruct;
        return arg;
    }
    if (lhs_ty->id == TypeFuncTy) {
        if (rhs_ty->id == TypePtr) return arg;
        else error(arg, err_expected, "function pointer!");
    }
    if (lhs_ty->id == TypeCStr) {
        if (rhs_ty->id == TypeCStr) return arg;
        else if (rhs_ty->id == TypeNull) return newCast(arg, lhs_ty);
        else error(arg, err_expected, "c-string!");
    }

    auto result = TypeConv[rhs_ty->id][lhs_ty->id];
    auto msg_id = note_no_error;
    switch (result)
    {
    case tc_same:
        if (rhs_ty->id == TypeNull) arg = newCast(arg, lhs_ty); 
        break;
    case tc_i_ii: 
    case tc_i_if:
    case tc_i_ff: arg = newCast(arg, lhs_ty); break;
    case tc_e_ff:
        if (arg->id < TypeType) {
            Type *ty = (Type *)arg;
            if (ty->data.fvalue <= FLT_MAX) ty->id = TypeFloat;
        } break;
    case tc_fail: msg_id = err_conv_type; break;
    default: msg_id = err_no_implicit_conv; break;
    }
    if (msg_id != note_no_error)
        error(arg, msg_id, TypeName[rhs_ty->id], TypeName[lhs_ty->id]);

    return arg;
}

Node * Parser::parseFnCall(FuncType *ftype, Function *fn, Token *tok, Token **rest) {
    if (!ftype) ftype = fn->type;
    auto call = newNode<FnCall>(NodeFnCall, tok);
    call->type = ftype;
    call->fn = fn;
    Node head{};
    Node *cur = &head;
    if (ftype->attrs & FuncVariadic) {
        if (tok->next->id == TokenIdLBracket) {
            tok = tok->next;
            int n = 0;
            do {
                auto arg = parseExpr(tok->next, &tok);
                cur = cur->next = arg;
                n++;
            } while (tok->next->id != TokenIdRBracket);
            tok = tok->next;
            call->n_args = n;
        } else {
            cur->next = parseExpr(tok->next, &tok);
            call->n_args = 1;
        }
    } else {
        auto param = ftype->params;
        if (ftype->isThrowable()) param = param->next;
        while (param) {
            auto arg = parseExpr(tok->next, &tok);
            cur = cur->next = checkArgType(arg, ((Variable *)param)->type);
            param = param->next;
        }
    }
    call->args = head.next;
    auto caller = curCtx->func;
    if (call->n_args > caller->n_typed_value) caller->n_typed_value = call->n_args;
    if (fn->id == TypeFunc) {
        if (!caller->calls) caller->calls = new std::vector<FnCall *>;
        caller->calls->push_back(call);
    }
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
            auto op_tok = tok->next;
            node = newNode<BinaryOp>(node_id, lhs, parseUnaryExpr(tok->next->next, &tok));
            node->tok = op_tok;
            lhs = node;
        } else {
            break;
        }
    }
    *rest = tok;
    return node ? node : lhs;
}

char Parser::scanByte(const char **bp, int *err) {
    static constexpr std::array<std::pair<const StringLiteral, char>, 8> Esc_Names = {{
        {"line",   10},
        {"tab",    9},
        {"page",   12},
        {"escape", 27},
        {"esc",    27},
        {"back",   8},
        {"del",    127},
        {"null",   0}
    }};
    
    auto c = **bp;
	(*bp)++;
	if (c != '^') return c;

	// Must be ^ escaped char:
	c = **bp;
	(*bp)++;

    char n;
    char lex;
    const char *cp;
    switch (c) {

	case 0:
		n = 0;
		break;

	case '/':
		n = '\n';
		break;

	case '^':
		n = c;
		break;

    case '-':
		n = '\t';
		break;

	case '!':
		n = '\036'; // record separator
		break;

	case '(':	// ^(tab) ^(1234)
		// Check for hex integers ^(1234):
		cp = *bp; // restart location
		n = 0;
		while ((lex = Lex_Map[*cp]) > LEX_WORD) {
			c = lex & LEX_VALUE;
			if (!c && lex < LEX_NUMBER) break;
			n = (n << 4) + c;
			cp++;
		}
		if ((cp - *bp) > 4) {*err = -1; return 0;}
		if (*cp == ')') {
			cp++;
			*bp = cp;
			return n;
		}

		// Check for identifiers:
		for (auto &[name, val] : Esc_Names) {
            if (0 == name.compare_insensitive(StringRef(*bp, name.size()))) {
                *bp += name.size();
                if (**bp == ')') {
                    (*bp)++;
                    return val;
                }
            }
		}
        *err = -1;
		return 0;

    default:
		n = toupper(c);
		if (n >= '@' && n <= '_') n -= '@';
		else if (n == '~') n = 0x7f; // special for DEL
		else n = c;  // includes: ^{ ^} ^"
    }

	return n;
}

Node * Parser::parseBool(Token *tok) {
    Type *ty = newNode<Type>(TypeBool, tok);
    ty->data.value = tok->id == TokenIdTrue ? 1 : 0;
    ty->size = 1;
    return ty;
}

Node * Parser::parseInteger(Token *tok) {
    Type *ty = newNode<Type>(TypeInt, tok);
    ty->size = 4;
    if (tok->toInteger(ty->data.value))
        comp.error(tok, err_int_overflow);
    return ty;
}

Node * Parser::parseByte(Token *tok) {
    auto p = &(tok->loc);
    int err = 0;
    auto chr = scanByte(p, &err);
    if (err == -1) comp.error(tok, err_invalid_token);
    auto ty = newNode<Type>(TypeByte, tok);
    ty->data.chr = chr;
    ty->size = 1;
    return ty;
}

Node * Parser::parseBinary(Token *tok) {
    auto node = newNode<Array>(NodeArray, tok);
    auto output = new SmallString<16>();
    auto n = tok->len;
    const char *p = tok->loc;
    int accum = 0;
    uint64_t count = 0;
    while (n > 0) {
        auto lex = Lex_Map[*p];
        if (lex > LEX_WORD) {
            auto val = lex & LEX_VALUE;
            accum = (accum << 4) + val;
            if (count++ & 1) output->push_back((char)accum);
        }
        p++;
        n--;
    }
    node->data = output->data();
    node->num = output->size();
    node->type = &byteTy;
    return node;
}

Node * Parser::parseString(Token *tok) {
    auto ty = newNode<Type>(TypeCStr, tok);
    ty->data.str = tok->loc;
    if (tok->flags & TkFlagEscapedString) {
        const char *src = tok->loc;
        int len = tok->len;
        int n = 0;
        int chr;
        const char *end = src + len;
        auto dst = new char[len];

        while (src < end) {
            chr = *src;
            if (chr == '^') {
                int err = 0;
                chr = scanByte(&src, &err);
                if (err == -1) comp.error(src, err_invalid_token);
                src--;
            }
            dst[n] = chr;
            n++;
            src++;
        }
        ty->data.str = dst;
        tok->len = n;
    }
    return ty;
}

Node * Parser::parseLiteralArray(Token *tok, Token **rest) {
    auto node = newNode<LitArray>(NodeLiteralArray, tok);
    node->is_homogenous = true;
    tok = tok->next;
    uint32_t n = 0;
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdRBracket) {
        n++;
        cur = cur->next = parseUnaryExpr(tok, &tok);
        tok = tok->next;
    }
    *rest = tok;
    node->vals = head.next;
    node->num = n;

    cur = head.next;
    auto id = cur->id;
    while (cur) {
        if (id != cur->id) node->is_homogenous = false;
        id = cur->id;
        cur = cur->next;
    }
    return node;
}

// type-cast = "as" (type | "[" type "]") "keep"? expression
Node * Parser::parseCast(Token *tok, Token **rest) {
    auto node = newNode<Cast>(NodeCast, tok);
    tok = tok->next;

    node->type = parseType(tok, &tok, tok->id == TokenIdLBracket);
    if (tok->id == TokenIdWord && tok->equals("keep")) {
        node->is_signed = true;     // reuse this field
    }
    node->expr = parseExpr(tok, rest);
    return node;
}

Node * Parser::parseDeclare(Token *tok, Token **rest) {
    auto node = newNode<TypedNode>(NodeDeclare, tok);
    node->type = parseType(tok->next, rest, false);
    auto end = *rest;
    while (tok->next != end) tok = tok->next;
    *rest = tok;
    return node;
}

Node * Parser::parseNot(Token *tok, Token **rest) {
    tok = tok->next;
    auto node = newNode<Unary>(NodeNot, tok);
    node->value = parseExpr(tok, rest);
    return node;
}

Node * Parser::parseSizeOf(Token *tok, Token **rest) {
    tok = tok->next;
    auto node = newNode<Unary>(NodeSizeOf, tok);

    // size? pointer!
    if (tok->id == TokenIdWord && tok->equals("pointer!") && tok->next->id != TokenIdLBracket) {
        *rest = tok;
        node->value = &uintPtrTy;
        return node;
    }

    isSizeOf = true;
    auto ty = parseType(tok, rest, false, true, false);
    isSizeOf = false;

    Node *var;
    // @@ we have to do this because `rest` points to the token beyond end token
    if (ty) {
        auto end = *rest;

        while (tok->next != end) {
            tok = tok->next;
        }
        *rest = tok;

        if (ty->id == TypeStructUnresolved) resolveStruct((Type *)ty, curCtx);
        node->value = (Node *)ty;
    } else {
        Identifier *ident = (Identifier *)parseExpr(tok, rest);
        if (ident->id == NodeIdent) {
            auto var = ident->var;
            if (var->id == NodeVar && var->hasAttr(VarGlobal) && var->size > 0) {
                Type *ty = newNode<Type>(TypeInt, tok);
                ty->data.value = var->size;
                return ty;
            }
        }
        node->value = ident;
    }

    return node;
}

Node * Parser::parseUnaryExpr(Token *tok, Token **rest) {
    Context *ctx = nullptr;
    Node *node = nullptr;
    Type *ty;
    bool is_set;
    switch (tok->id) {
        case TokenIdWord:
        label_word:
            LOG() << tok->str() << '\n';
            node = findDecl(tok, &ctx);
            if (!node) comp.error(tok, err_undeclared_name, tok->str());
            if (node->id == NodeVar) {
                auto var = (Variable *)node;
                auto ty = var->type;
                // dereference func ptr
                if (ty && ty->id == TypeFuncTy) {
                    node = parseFnCall((FuncType *)ty, (Function *)node, tok, &tok);
                } else {
                    auto ident = newNode<Identifier>(NodeIdent, tok);
                    ident->var = var;
                    node = ident;
                }
            } else if (node->id == TypeFunc) {
                node = parseFnCall(nullptr, (Function *)node, tok, &tok);
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
                    node = newPtrNode(node, tok);
                } else {
                    comp.error(tok, err_expected, "variable or function");
                }
            } else {
                comp.error(tok, err_undeclared_name, tok->str());
            }
            break;
        case TokenIdInteger:
        case TokenIdHex:
            node = parseInteger(tok);
            break;
        case TokenIdFloat:
            ty = newNode<Type>(TypeDouble, tok);
            ty->size = 8;
            node = ty;
            if (tok->flags & TKFlagSpecialFloat) {
                if (tok->equals("1.#inf")) ty->data.fvalue = std::numeric_limits<double>::infinity();
                else if (tok->equals("1.#nan")) ty->data.fvalue = std::numeric_limits<double>::quiet_NaN();
            } else if (tok->toDouble(ty->data.fvalue)) {
                comp.error(tok, err_float_overflow);
            }
            break;
        case TokenIdByte:
            node = parseByte(tok);
            break;
        case TokenIdBinary:
            node = parseBinary(tok);
            break;
        case TokenIdString:
            node = parseString(tok);
            break;
        case TokenIdTrue:
        case TokenIdFalse:
            node = parseBool(tok);
            break;
        case TokenIdLParen:
            node = parseExpr(tok->next, &tok);
            tok = tok->next;
            expect(tok, TokenIdRParen, ')');
            break;
        case TokenIdAny:      node = parseAny(tok, &tok); break;
        case TokenIdAll:      node = parseAll(tok, &tok); break;
        case TokenIdEither:   node = parseEither(tok, &tok); break;
        case TokenIdCase:     node = parseCase(tok, &tok); break;
        case TokenIdSwitch:   node = parseSwitch(tok, &tok); break;
        case TokenIdAs:       node = parseCast(tok, &tok); break;
        case TokenIdDeclare:  node = parseDeclare(tok, &tok); break;
        case TokenIdSize:     node = parseSizeOf(tok, &tok); break;
        case TokenIdNot:      node = parseNot(tok, &tok); break;
        case TokenIdNull:     node = newNode<Node>(NodeNull, tok); break;
        case TokenIdLBracket: node = parseLiteralArray(tok, &tok); break;
        case TokenIdFunc:  // anonymous function
        case TokenIdAlias:
        default:
            if (tok->sub_id == TokenIdKeyword) goto label_word;
            break;
    }
    *rest = tok;
    return node;
}

Node * Parser::parseExpr(Token *tok, Token **rest) {
    Node *node = parseUnaryExpr(tok, &tok);
    if (node) node = parseBinaryOp(node, tok, &tok);
    else comp.error(tok, err_invalid_expr);
    *rest = tok;
    return node;
}

// assignment = lh-side+ expression
Node * Parser::parseAssignment(Token *tok, Token **rest, bool is_in_func) {
    LOG() << tok->str() << '\n';
    Context *ctx = nullptr;
    Node *node = nullptr;
    bool is_set = false;
    if (tok->id == TokenIdSetWord || (tok->sub_id == TokenIdKeyword && tok->next->id == TokenIdSetColon)) {
        Context *ctx;
        node = findVar(tok, &ctx);
        if (is_in_func) {
            if (!node) {
                comp.error(tok, err_undeclared_name, tok->str());
            } else if (node->id != NodeVar) {
                comp.error(tok, err_symbol_declared, tok->str());
            }
        } else {    // in a context
            if (withNS.empty()) node = nullptr;
            node = addVar(curCtx, tok, (Variable *)node);
        }

        if (tok->sub_id == TokenIdKeyword) tok = tok->next;
        is_set = true;
    } else if (tok->id == TokenIdPath) {
        node = parsePath(tok, &tok, is_set);
        auto bin = parseBinaryOp(node, tok, &tok);
        if (node != bin && is_set) error(bin, err_invalid_expr);
        node = bin;
        *rest = tok;
    } else {
        node = parseExpr(tok, rest);
    }

    if (is_set)
        node = newNode<Assignment>(NodeAssign, node, parseAssignment(tok->next, rest, is_in_func));
    return node;
}

Node * Parser::parseAnyAll(Token *tok, Token **rest, NodeId id) {
    addCurFuncAttr(FuncHasAnyAll);
    auto node = newNode<Any>(id, tok);
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
    Either *last_node = nullptr;
    while (tok->id != TokenIdEof) {
        if (tok->id == TokenIdTrue) {   // catch-all rule
            auto true_case = parseCode(tok->next, &tok, curCtx);
            *rest = tok->next;
            expect(*rest, TokenIdRBracket, ']');
            if (last_node) {
                last_node->else_body = true_case;
                return node;
            } else {
                return true_case;
            }
        }

        auto case_node = newNode<Either>(NodeEither, tok);
        case_node->cond = parseExpr(tok, &tok);

        tok = tok->next;
        expect(tok, TokenIdLBracket, '[');
        case_node->then_body = parseCode(tok, &tok, curCtx);

        if (!node->cases) node->cases = case_node;

        if (last_node) last_node->else_body = case_node;
        last_node = case_node;

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
    addCurFuncAttr(FuncHasLoop);
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
    if (curFunc->type->return_type->id == TypeVoid)
        comp.error(tok, err_return_type);
    auto node = newNode<Return>(NodeReturn, tok);
    node->expr = parseExpr(tok->next, rest);
    return node;
}

Node * Parser::parseExit(Token *tok, Token **rest) {
    if (curFunc->type->return_type->id != TypeVoid)
        comp.error(tok, err_requires_return);
    return newNode<Node>(NodeExit, tok);
}

Node * Parser::parseThrow(Token *tok, Token **rest) {
    auto node = newNode<Throw>(NodeThrow, tok);
    node->value = parseExpr(tok->next, rest);
    if (!curCtx->func->isThrowable()) {
        // add a hidden param for exception handling
        auto fn_ty = curCtx->func->type;
        auto param = newNode<Variable>(NodeVar, &hiddenParamTok);
        param->type = &uintPtrTy;
        param->next = fn_ty->params;
        fn_ty->params = param;
        fn_ty->n_params++;
        fn_ty->addAttr(FuncThrow);
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

Node * Parser::parseCode(Token *tok, Token **rest, Context *ctx, bool is_func_body, bool is_block) {
    if (is_block) tok = skip(tok, TokenIdLBracket, '[');
    curCtx = ctx;
    Node head{};
    Node *cur = &head;
    while (tok->id != TokenIdEof) {
        if (tok->loc) LOG() << tok->str() << '\n';
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
        case TokenIdExit: node = parseExit(tok, &tok); break;
        case TokenIdWith: {
            auto saved_ns = withNS.size();
            tok = tok->next;
            SmallVector<Context *> ns_vec;
            if (tok->id == TokenIdWord) {
                auto ns = findContext(tok);
                if (!ns) comp.error(tok, err_undeclared_ns, tok->str());
                ns_vec.push_back(ns);
            } else if (tok->id == TokenIdLBracket) {
                tok = tok->next;
                while (tok->id != TokenIdRBracket) {
                    auto ns = findContext(tok);
                    if (ns == nullptr) comp.error(tok, err_undeclared_ns, tok->str());
                    ns_vec.push_back(ns);
                    tok = tok->next;
                }
            }
            if (!saved_ns) {
                withNS.append(ns_vec.rbegin(), ns_vec.rend());
                withNS.push_back(curCtx);
            } else {
                withNS.insert(withNS.begin(), ns_vec.rbegin(), ns_vec.rend());
            }
            node = parseCode(tok->next, &tok, ctx, is_func_body);

            if (saved_ns) withNS.erase(withNS.begin(), withNS.begin() + ns_vec.size());
            else withNS.clear();

            curCtx = ctx;
            tok = tok->next;
            cur->next = node;
            while (node) {
                cur = node;
                node = node->next;
            }
            continue;
        }

        case TokenIdUse: tok = tok->next; break;
        case TokenIdRBracket:
            if (is_block) {
                *rest = tok;
                return head.next;
            }
        case TokenIdSetWord: {
            auto id = tok->next->id;
            if (id == TokenIdContext) {
                Context *new_ctx = findContext(tok);
                new_ctx->func = ctx->func;
                node = parseCode(tok->next->next, &tok, new_ctx, false);
                curCtx = ctx;
                tok = tok->next;
                cur->next = node;
                while (node) {
                    cur = node;
                    node = node->next;
                }
                continue;
            }
            if (id == TokenIdFunc || id == TokenIdAlias) {   // already handled in collect phase
                tok = tok->next->next;
                continue;
            }
        } // fall-thru
            
        // assignment or expression
        default:
            node = parseAssignment(tok, &tok, is_func_body);
            LOG() << "parseassign: " << tok->str() << '\n';
            if (node && node->id == NodeAssign) {
                addVarType(node);
            }
            break;
        }
        tok = tok->next;
        if (node)
            cur = cur->next = node;
    }
    *rest = tok;
    return head.next;
}

void Parser::resolveWithDeclarations(Context *ctx, bool parse_func) {
    SmallVector<Context *> ns_vec;
    for (auto with : withDecls) {
        for (auto name : with->ns) {
            auto ns = findContext(name);
            if (!ns) comp.error(name, err_undeclared_ns, name->str());
            ns_vec.push_back(ns);
        }
        withNS.append(ns_vec);
        withNS.push_back(with->ctx);
        withNS.push_back(globalCtx);
        for (auto node : with->decls) {
            if (parse_func && node->id == TypeFunc) {
                parseFunc((Function *)node, ctx);
            } else {
                Type *ty = (Type *)node;
                if (!resolveStruct(ty, ctx)) {
                    resolveFuncSpec(ty);
                }
            }
        }
        withNS.clear();
    }
}

Node * Parser::parseCodeBlock(Token *tok, Context *ctx, bool is_func_body) {
    (LOG() << "start\n").indent();

    curCtx = ctx;
    curFunc = ctx->func;
    tok = collectDeclarations(tok, ctx, is_func_body, is_func_body);

    dumpTokens(tok, "After collectDeclarations");

    resolveWithDeclarations(ctx, false);
    // resolve struct and function spec
    resolveDeclarations(ctx);

    LOG() << "parseCode start\n";
    Node *ast = parseCode(tok, &tok, ctx, is_func_body, is_func_body);
    LOG() << "parseCode end\n";

    dumpAst(ast);

    resolveWithDeclarations(ctx, true);
    parseFunctions(ctx);

    curCtx = ctx;

    curFunc = ctx->func;
    LOG() << "ParseCodebb.... " << curFunc->tok->str() << '\n';
    analyzeType(ast);

    LOG.unindent(); LOG() << "end\n";
    return ast;
}

Context * Parser::parse(Token *tok) {
    Context *ctx = globalCtx = newContext(tok);
    Function *mainFunc = newFunction(tok);
    mainFunc->type->return_type = &voidTy;
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
    llvm::errs() << "Params: " << func->type->n_params << " ";
    auto p = func->type->params;
    while (p) {
        auto ty = ((Variable *)p)->type;
        llvm::errs() << p->tok->str() << ": " << TypeName[ty->id] << " ";
        p = p->next;
    }
    //llvm::errs() << "\nLocals: " << func->n_locals << " ";
    p = func->locals;
    while (p) {
        auto ty = ((Variable *)p)->type;
        llvm::errs() << p->tok->str() << ": " << TypeName[ty->id] << " ";
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
        errs() << NodeName[node->id] << " " << node->tok->str() << "\n";
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
