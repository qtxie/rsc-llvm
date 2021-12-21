/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
*/

#ifndef RED_SYSTEM_TOKEN_H
#define RED_SYSTEM_TOKEN_H

#include "Buffer.h"

enum TokenId {
    TokenIdError,
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
    TokenIdMacroCall,
    TokenIdMacroCase,
    TokenIdMacroDefine,
    TokenIdMacroEither,
    TokenIdMacroEnum,
    TokenIdMacroExport,
    TokenIdMacroImport,
    TokenIdMacroInclude,
    TokenIdMacroIf,
    TokenIdMacroInline,
    TokenIdMacroSwitch,
    TokenIdMacroSyscall,
    TokenIdMacroU16,
    TokenIdMacroVerbose,
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
    TokenIdEOF,

    TokenIdCount,
};

struct Hideset {
    Hideset *next;
    char *name;
};

struct Token {
    TokenId id;         // Token ID
    int len;            // Token length
    const char *loc;    // Token location
    Token *next;        // Next Token
    Hideset *hideset;   // For macro expansion
};

typedef uint32_t TokenIndex;
typedef Buffer<Token> TokenBuffer; 

#endif /* RED_SYSTEM_TOKEN_H */
