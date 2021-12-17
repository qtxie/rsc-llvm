/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: 
*/

#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/WithColor.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Target/TargetMachine.h>

#include "version.h"
#include "compilation.h"

using namespace llvm;

static codegen::RegisterCodeGenFlags CGF;

static cl::list<std::string>
    InputFiles(cl::Positional, cl::desc("<input-files>"));

static cl::opt<std::string>
    MTriple("mtriple",
            cl::desc("Override target triple for module"));

static cl::opt<bool>
    EmitLLVM("emit-llvm",
             cl::desc("Emit IR code instead of assembler"),
             cl::init(false));

static cl::opt<signed char> OptLevel(
    cl::desc("Setting the optimization level:"),
    cl::ZeroOrMore,
    cl::values(
        clEnumValN(3, "O", "Equivalent to -O3"),
        clEnumValN(0, "O0", "Optimization level 0"),
        clEnumValN(1, "O1", "Optimization level 1"),
        clEnumValN(2, "O2", "Optimization level 2"),
        clEnumValN(3, "O3", "Optimization level 3"),
        clEnumValN(-1, "Os",
                   "Like -O2 with extra optimizations "
                   "for size"),
        clEnumValN(
            -2, "Oz",
            "Like -Os but reduces code size further")),
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
    OS << "  Default target: "
        << llvm::sys::getDefaultTargetTriple() << "\n";
    std::string CPU(llvm::sys::getHostCPUName());
    OS << "  Host CPU: " << CPU << "\n";
    OS << "\n";
    OS.flush();
    llvm::TargetRegistry::printRegisteredTargetsForVersion(
        OS);
    exit(EXIT_SUCCESS);
}

llvm::TargetMachine *
createTargetMachine(const char *Argv0) {
  llvm::Triple Triple = llvm::Triple(
      !MTriple.empty()
          ? llvm::Triple::normalize(MTriple)
          : llvm::sys::getDefaultTargetTriple());

  llvm::TargetOptions TargetOptions =
      codegen::InitTargetOptionsFromCodeGenFlags(
          Triple);
  std::string CPUStr = codegen::getCPUStr();
  std::string FeatureStr = codegen::getFeaturesStr();

  std::string Error;
  const llvm::Target *Target =
      llvm::TargetRegistry::lookupTarget(
          codegen::getMArch(), Triple, Error);

  if (!Target) {
    llvm::WithColor::error(llvm::errs(), Argv0)
        << Error;
    return nullptr;
  }

  llvm::TargetMachine *TM = Target->createTargetMachine(
      Triple.getTriple(), CPUStr, FeatureStr,
      TargetOptions,
      llvm::Optional<llvm::Reloc::Model>(
          codegen::getRelocModel()));
  return TM;
}

std::string output_file_name(StringRef InputFilename) {
  CodeGenFileType FileType = codegen::getFileType();
  std::string OutputFilename;
  if (InputFilename == "-") {
    OutputFilename = "-";
  } else {
    if (InputFilename.endswith(".reds") ||
        InputFilename.endswith(".reds"))
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
        Compilation comp{file};
        comp.run();
    }
    return 0;
}