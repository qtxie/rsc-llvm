/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#include "Parser.h"

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
 *  param             = (word | typed-word) doc-string?
 *  typed-word        = word "[" type "]"
 *  local-var         = typed-word
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


void Parser::collectDeclarations(const Token *tok, Context *ctx) {
    while (tok) {
        
        tok = tok->next;
    }
}

Program * Parser::parse(const Token *tok) {
    Context *ctx = newNode<Context>();
    program->ctx = ctx;

    collectDeclarations(tok, ctx);

    return nullptr;
}
