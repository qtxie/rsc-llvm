/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_TOKENIZER_H
#define RED_SYSTEM_TOKENIZER_H

#include <cstdint>
#include <vector>
#include "llvm.h"

class Compilation;

enum TokenId {
    TokenIdBitShiftLeft,
    TokenIdBitShiftRight,           // >>
    TokenIdBitShiftRightLogical,    // >>>
    TokenIdBuiltin,
    TokenIdCharLiteral,
    TokenIdCmpEq,
    TokenIdCmpGreaterOrEq,
    TokenIdCmpGreaterThan,
    TokenIdCmpLessOrEq,
    TokenIdCmpLessThan,
    TokenIdCmpNotEq,
    TokenIdComma,
    TokenIdEq,
    TokenIdFloatLiteral,
    TokenIdGetPath,
    TokenIdGetWord,
    TokenIdHash,
    TokenIdIntLiteral,
    TokenIdKeywordAlias,
    TokenIdKeywordAll,
    TokenIdKeywordAnd,
    TokenIdKeywordAny,
    TokenIdKeywordAs,
    TokenIdKeywordAssert,
    TokenIdKeywordBreak,
    TokenIdKeywordCase,
    TokenIdKeywordCatch,
    TokenIdKeywordContext,
    TokenIdKeywordContinue,
    TokenIdKeywordDeclare,
    TokenIdKeywordEither,
    TokenIdKeywordExit,
    TokenIdKeywordEnum,
    TokenIdKeywordFalse,
    TokenIdKeywordFunc,
    TokenIdKeywordFunction,
    TokenIdKeywordIf,
    TokenIdKeywordLoop,
    TokenIdKeywordNot,
    TokenIdKeywordNull,
    TokenIdKeywordOr,
    TokenIdKeywordPop,
    TokenIdKeywordPush,
    TokenIdKeywordReturn,
    TokenIdKeywordSize,     // size?
    TokenIdKeywordStruct,
    TokenIdKeywordSwitch,
    TokenIdKeywordThrow,
    TokenIdKeywordTrue,
    TokenIdKeywordUntil,
    TokenIdKeywordUse,
    TokenIdKeywordWhile,
    TokenIdKeywordWith,
    TokenIdKeywordXor,
    TokenIdLBrace,
    TokenIdLBracket,
    TokenIdLParen,
    TokenIdMinus,
    TokenIdModulo,
    TokenIdQuestion,
    TokenIdPath,
    TokenIdPercent,
    TokenIdPlus,
    TokenIdRBrace,
    TokenIdRBracket,
    TokenIdRParen,
    TokenIdSemicolon,
    TokenIdSetPath,
    TokenIdSetWord,
    TokenIdSlash,
    TokenIdStar,
    TokenIdStringLiteral,
    TokenIdMultilineStringLiteralLine,
    TokenIdIdentifier,

    TokenIdCount,
};

struct Hideset {
    Hideset *next;
    char *name;
};

struct Token {
    TokenId id;         // Token ID
    const char *loc;    // Token location
    int len;            // Token length
    Hideset *hideset;   // For macro expansion
};

typedef uint32_t TokenIndex;
typedef std::vector<Token> TokenList;

void tokenize(Compilation& ctx, const char *buffer);

#endif /* RED_SYSTEM_TOKENIZER_H */
