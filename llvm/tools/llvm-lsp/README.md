# LLVM LSP server

## Build

```bash
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
ninja -j 4 -C build llvm-lsp
```
