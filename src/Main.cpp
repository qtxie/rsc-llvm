/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: 
*/

#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/WithColor.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Target/TargetMachine.h>

#include "Version.h"
#include "Compilation.h"

using namespace llvm;

static codegen::RegisterCodeGenFlags CGF;

static cl::list<std::string>
    InputFiles(cl::Positional, cl::desc("<input-files>"));

static cl::opt<std::string>
    MTriple("mtriple",
            cl::desc("Override target triple for module"));

static cl::opt<bool>
    EmitLLVM("emit-llvm-ir",
             cl::desc("Emit IR code instead of assembler"),
             cl::init(false));

static cl::opt<signed char> OptLevel(
    "O", cl::desc("Setting the optimization level:"),
    cl::values(
        clEnumValN(0, "debug", "(default) No Optimizations"),
        clEnumValN(1, "release", "Optimize for fast binary"),
        clEnumValN(2, "release-small", "like \'release\', but reduces code size")),
    cl::init(0));

static cl::opt<bool>
    DebugPM("debug-pass-manager", cl::Hidden,
            cl::desc("Print PM debugging information"));

static cl::opt<std::string> PassPipeline(
    "passes",
    cl::desc("A description of the pass pipeline"));

static cl::opt<std::string> PipelineStartEPPipeline(
    "passes-ep-pipeline-start",
    cl::desc("Pipeline start extension point"));

void printVersion(llvm::raw_ostream &OS) {
    OS << "Red/System compiler v" << RSC_VERSION << "\n";
    OS << "  Default target: " << llvm::sys::getDefaultTargetTriple() << "\n";
    std::string CPU(llvm::sys::getHostCPUName());
    OS << "  Host CPU: " << CPU << "\n\n";
    OS.flush();
    llvm::TargetRegistry::printRegisteredTargetsForVersion(OS);
    exit(EXIT_SUCCESS);
}

TargetMachine * createTargetMachine(const char *Argv0) {
  Triple triple(!MTriple.empty() ? Triple::normalize(MTriple) : sys::getDefaultTargetTriple());

  TargetOptions targetOptions = codegen::InitTargetOptionsFromCodeGenFlags(triple);
  std::string CPUStr = codegen::getCPUStr();
  std::string featureStr = codegen::getFeaturesStr();

  std::string err;
  const Target *target = TargetRegistry::lookupTarget(codegen::getMArch(), triple, err);

  if (!target) {
      WithColor::error(llvm::errs(), Argv0) << err;
      return nullptr;
  }

  llvm::TargetMachine *TM = target->createTargetMachine(
      triple.getTriple(), CPUStr, featureStr, targetOptions,
      llvm::Optional<llvm::Reloc::Model>(codegen::getRelocModel()));
  return TM;
}

std::string outFileName(StringRef InputFilename) {
    CodeGenFileType FileType = codegen::getFileType();
    std::string OutputFilename;
    if (InputFilename == "-") {
        OutputFilename = "-";
    } else {
        if (InputFilename.endswith(".reds") || InputFilename.endswith(".reds"))
            OutputFilename = InputFilename.drop_back(4).str();
        else
            OutputFilename = InputFilename.str();
        switch (FileType) {
        case CGFT_AssemblyFile:
            OutputFilename.append(EmitLLVM ? ".ll" : ".s");
            break;
        case CGFT_ObjectFile:
            OutputFilename.append(".o");
            break;
        case CGFT_Null:
            OutputFilename.append(".null");
            break;
        }
    }
    return OutputFilename;
}

int main(int Argc, const char **Argv) {
    llvm::InitLLVM X(Argc, Argv);

    //InitializeAllTargets();
    //InitializeAllTargetMCs();
    //InitializeAllAsmPrinters();
    //InitializeAllAsmParsers();

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    llvm::cl::SetVersionPrinter(&printVersion);
    llvm::cl::ParseCommandLineOptions(Argc, Argv);

    if (Argc == 1) {
        llvm::WithColor::error(llvm::errs(), Argv[0])
            << "no input files" << "\n";
        exit(EXIT_FAILURE);
    }

    llvm::TargetMachine *TM = createTargetMachine(Argv[0]);
    if (!TM) exit(EXIT_FAILURE);

    for (const auto &file : InputFiles) {
        Compilation comp(file, TM, EmitLLVM);
        comp.run();
    }

    delete TM;
    return 0;
}