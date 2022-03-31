# Red/System Compiler on LLVM

This is a quick and dirty implementation, for the following purposes:
1. Give us an optimizing compiler for Red/System in a relatively short amount of time.
2. Generate 64bit binary and support for static linking.

### Usage

```
redsc.exe --mode=release -L.\baselibs\x64\ -R.\src\runtime\ .\examples\test.reds
redsc.exe --mode=debug -L.\baselibs\x64\ -R.\src\runtime\ .\tests\integer-test.reds
```

### Download

https://github.com/red/redsc/actions/workflows/windows.yml

Click one of the workflow runs, there is an artifact `redsc-win-x64` on the page.

### Dev Note
Output LLVM IR:
```
redsc.exe --emit-llvm-ir filename.reds
```
List supported targets:
```
redsc.exe --version
```

### Build on Windows

### Build on Linux
```
chmod +x deps_ubuntu_20.04.sh
sudo ./deps_ubuntu_20.04.sh
bash <(wget https://xmake.io/shget.text -O -)
source ~/.xmake/profile
xmake -v
```