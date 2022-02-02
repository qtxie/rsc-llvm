/*
 * Copyright (c) 2021 Red Foundation. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License").
 * You may obtain a copy of the License at
 * https://github.com/red/red/blob/master/BSD-3-License.txt
 * 
 * Summary: 
*/

#include <filesystem>
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
#include <llvm/Support/ConvertUTF.h>
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
        llvm::Optional<Reloc::Model>(Reloc::Model::PIC_));
    return TM;
}

#ifndef _WIN32

#include <pwd.h>
#include <unistd.h>

static std::string getHome() {
	std::string res;
	int uid = getuid();
	const char* homeEnv = std::getenv("HOME");
	if ( uid != 0 && homeEnv) {
		//We only acknowlegde HOME if not root.
		res = homeEnv;
		return res;
	}
	struct passwd* pw = nullptr;
	struct passwd pwd;
	long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsize < 0) {
		bufsize = 16384;
	}
	std::vector<char> buffer;
	buffer.resize(bufsize);
	int error_code = getpwuid_r(uid, &pwd, buffer.data(), buffer.size(), &pw);
	if (error_code) {
        return res;
		//throw std::runtime_error("Unable to get passwd struct.");
	}
	const char* tempRes = pw->pw_dir;
	if (!tempRes) {
        return res;
		//throw std::runtime_error("User has no home directory");
	}
	res = tempRes;
	return res;
}

#endif

#ifdef _WIN32

// Make sure we don't bring in all the extra junk with windows.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// stringapiset.h depends on this
#include <windows.h>
// For SUCCEEDED macro
#include <winerror.h>
// For WideCharToMultiByte
#include <stringapiset.h>
// For SHGetFolderPathW and various CSIDL "magic numbers"
#include <shlobj.h>

std::string getCacheDir() {
    // KF_FLAG_CREATE force folder creation if not exists
    wchar_t *appdata = NULL;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &appdata) == S_OK) {
        std::string res;
        int actualSize = WideCharToMultiByte(CP_UTF8, 0, appdata, -1, nullptr, 0, nullptr, nullptr);
        if (actualSize > 0) {
            std::vector<char> buffer(actualSize);
            actualSize = WideCharToMultiByte(CP_UTF8, 0, appdata, -1, &buffer[0], static_cast<int>(buffer.size()), nullptr, nullptr);
            res = buffer.data();
        } else {
            WithColor::warning(llvm::errs()) << "Cannot get AppData folder\n";
        }
        CoTaskMemFree((void*)appdata);
        return res;
    }
}

#elif defined(__APPLE__)

std::string getCacheDir() {
    return getHome() + "/Library/Caches";
}

#else

std::string getCacheDir() {
    return getHome();
}
#endif

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

    using OptimizationLevel = typename PassBuilder::OptimizationLevel;
    OptimizationLevel optLevel;
    switch (OptLevel) {
        case 0:  optLevel = OptimizationLevel::O0; break;
        case 1:  optLevel = OptimizationLevel::O2; break;
        case 2:  optLevel = OptimizationLevel::Os; break;
        default: optLevel = OptimizationLevel::O1; break;
    }

    namespace fs = std::filesystem;
    auto dataDir = fs::path(getCacheDir()) / ".redrs";
    std::filesystem::create_directories(dataDir);

    for (const auto &file : InputFiles) {
        Compilation comp(file, dataDir.string(), TM, optLevel, EmitLLVM);
        comp.run();
    }

    delete TM;
    return 0;
}