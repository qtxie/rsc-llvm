# Red/System Compiler on LLVM

This is a quick and dirty implementation, for the following purposes:
1. Give us an optimizing compiler for Red/System in a relatively short amount of time.
2. Generate 64bit binary and support for static linking.

### Usage

```
redsc.exe -L.\baselibs\x64\ .\test\test.reds
```

### Build on Windows

1. [Download LLVM libraries](https://github.com/qtxie/bigfiles/raw/main/llvm%2Bclang%2Blld-13.0.0-x86_64-windows-msvc-debug.7z).
2. Install [Build Tools for Visual Studio 2022](https://aka.ms/vs/17/release/vs_BuildTools.exe). Be sure to select "Desktop development with C++" when prompted.
3. Install [xmake](https://xmake.io/#/getting_started).
4. Unzip the LLVM libraries 7z file. Modify the lib paths in `xmake.lua` (line 179 - 185) in your redsc source checkout.
    ```
    if is_mode("debug") then 
        add_linkdirs("../../../llvm+clang+lld-13.0.0-x86_64-windows-msvc-debug/lib")
        add_includedirs("../../../llvm+clang+lld-13.0.0-x86_64-windows-msvc-debug/include")
    elseif is_mode("release") then
        add_linkdirs("../../../llvm+clang+lld-13.0.0-x86_64-windows-msvc-release-mt/lib")
        add_includedirs("../../../llvm+clang+lld-13.0.0-x86_64-windows-msvc-release-mt/include")
    end
    ```
5. In CMD in your redsc source checkout.
    ```
    xmake f -m debug
    xmake
    ```
    You now have the `redsc.exe` binary at `build\windows\x64\debug` folder.

### Build on Linux
TBD

### Dev Note
Output LLVM IR:
```
redsc.exe --emit-llvm-ir filename.reds
```