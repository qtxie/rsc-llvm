#ifndef RED_SYSTEM_CODEGEN_H
#define RED_SYSTEM_CODEGEN_H

#include "LLVM.h"
#include "Token.h"
#include <llvm/IR/LLVMContext.h>

class Compilation;

class CodeGen
{
private:
    Compilation &comp;
    llvm::LLVMContext llvm_ctx;

public:
    CodeGen(Compilation &c) : comp(c) {};
    CodeGen(Compilation &&) = delete;  // prevents rvalue binding
};

#endif /* RED_SYSTEM_CODEGEN_H */
