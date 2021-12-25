if is_plat("windows") then
    add_defines("_HAS_EXCEPTIONS=0", "GTEST_HAS_RTTI=0")
    if is_mode("debug") then
        add_cxflags("/MTd", "/FS", "/Z7", "/Od")
        add_ldflags("/DEBUG")
    end
end

option("llvm")
    set_default(true)
    add_links(
        "LLVMWindowsManifest",
        "LLVMXRay",
        "LLVMLibDriver",
        "LLVMDlltoolDriver",
        "LLVMCoverage",
        "LLVMLineEditor",
        "LLVMXCoreDisassembler",
        "LLVMXCoreCodeGen",
        "LLVMXCoreDesc",
        "LLVMXCoreInfo",
        "LLVMX86Disassembler",
        "LLVMX86AsmParser",
        "LLVMX86CodeGen",
        "LLVMX86Desc",
        "LLVMX86Info",
        "LLVMWebAssemblyDisassembler",
        "LLVMWebAssemblyAsmParser",
        "LLVMWebAssemblyCodeGen",
        "LLVMWebAssemblyDesc",
        "LLVMWebAssemblyUtils",
        "LLVMWebAssemblyInfo",
        "LLVMSystemZDisassembler",
        "LLVMSystemZAsmParser",
        "LLVMSystemZCodeGen",
        "LLVMSystemZDesc",
        "LLVMSystemZInfo",
        "LLVMSparcDisassembler",
        "LLVMSparcAsmParser",
        "LLVMSparcCodeGen",
        "LLVMSparcDesc",
        "LLVMSparcInfo",
        "LLVMRISCVDisassembler",
        "LLVMRISCVAsmParser",
        "LLVMRISCVCodeGen",
        "LLVMRISCVDesc",
        "LLVMRISCVInfo",
        "LLVMPowerPCDisassembler",
        "LLVMPowerPCAsmParser",
        "LLVMPowerPCCodeGen",
        "LLVMPowerPCDesc",
        "LLVMPowerPCInfo",
        "LLVMNVPTXCodeGen",
        "LLVMNVPTXDesc",
        "LLVMNVPTXInfo",
        "LLVMMSP430Disassembler",
        "LLVMMSP430AsmParser",
        "LLVMMSP430CodeGen",
        "LLVMMSP430Desc",
        "LLVMMSP430Info",
        "LLVMMipsDisassembler",
        "LLVMMipsAsmParser",
        "LLVMMipsCodeGen",
        "LLVMMipsDesc",
        "LLVMMipsInfo",
        "LLVMLanaiDisassembler",
        "LLVMLanaiCodeGen",
        "LLVMLanaiAsmParser",
        "LLVMLanaiDesc",
        "LLVMLanaiInfo",
        "LLVMHexagonDisassembler",
        "LLVMHexagonCodeGen",
        "LLVMHexagonAsmParser",
        "LLVMHexagonDesc",
        "LLVMHexagonInfo",
        "LLVMBPFDisassembler",
        "LLVMBPFAsmParser",
        "LLVMBPFCodeGen",
        "LLVMBPFDesc",
        "LLVMBPFInfo",
        "LLVMAVRDisassembler",
        "LLVMAVRAsmParser",
        "LLVMAVRCodeGen",
        "LLVMAVRDesc",
        "LLVMAVRInfo",
        "LLVMARMDisassembler",
        "LLVMARMAsmParser",
        "LLVMARMCodeGen",
        "LLVMARMDesc",
        "LLVMARMUtils",
        "LLVMARMInfo",
        "LLVMAMDGPUDisassembler",
        "LLVMAMDGPUAsmParser",
        "LLVMAMDGPUCodeGen",
        "LLVMAMDGPUDesc",
        "LLVMAMDGPUUtils",
        "LLVMAMDGPUInfo",
        "LLVMAArch64Disassembler",
        "LLVMAArch64AsmParser",
        "LLVMAArch64CodeGen",
        "LLVMAArch64Desc",
        "LLVMAArch64Utils",
        "LLVMAArch64Info",
        "LLVMOrcJIT",
        "LLVMMCJIT",
        "LLVMJITLink",
        "LLVMInterpreter",
        "LLVMExecutionEngine",
        "LLVMRuntimeDyld",
        "LLVMOrcTargetProcess",
        "LLVMOrcShared",
        "LLVMDWP",
        "LLVMSymbolize",
        "LLVMDebugInfoPDB",
        "LLVMDebugInfoGSYM",
        "LLVMOption",
        "LLVMObjectYAML",
        "LLVMMCA",
        "LLVMMCDisassembler",
        "LLVMLTO",
        "LLVMPasses",
        "LLVMCFGuard",
        "LLVMCoroutines",
        "LLVMObjCARCOpts",
        "LLVMipo",
        "LLVMVectorize",
        "LLVMLinker",
        "LLVMInstrumentation",
        "LLVMFrontendOpenMP",
        "LLVMFrontendOpenACC",
        "LLVMExtensions",
        "LLVMDWARFLinker",
        "LLVMGlobalISel",
        "LLVMMIRParser",
        "LLVMAsmPrinter",
        "LLVMDebugInfoMSF",
        "LLVMDebugInfoDWARF",
        "LLVMSelectionDAG",
        "LLVMCodeGen",
        "LLVMIRReader",
        "LLVMAsmParser",
        "LLVMInterfaceStub",
        "LLVMFileCheck",
        "LLVMFuzzMutate",
        "LLVMTarget",
        "LLVMScalarOpts",
        "LLVMInstCombine",
        "LLVMAggressiveInstCombine",
        "LLVMTransformUtils",
        "LLVMBitWriter",
        "LLVMAnalysis",
        "LLVMProfileData",
        "LLVMObject",
        "LLVMTextAPI",
        "LLVMMCParser",
        "LLVMMC",
        "LLVMDebugInfoCodeView",
        "LLVMBitReader",
        "LLVMCore",
        "LLVMRemarks",
        "LLVMBitstreamReader",
        "LLVMBinaryFormat",
        "LLVMSupport",
        "LLVMDemangle"
    )
    add_linkdirs("../../../llvm+clang+lld-13.0.0-x86_64-windows-msvc-debug/lib")
    add_includedirs("../../../llvm+clang+lld-13.0.0-x86_64-windows-msvc-debug/include")

set_languages("cxx20")

target("redsc")
    set_kind("binary")
    add_options("llvm")
    add_files("src/*.cpp")

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro defination
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

