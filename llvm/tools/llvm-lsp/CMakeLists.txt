set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  Support
  Analysis
  Passes
)

add_llvm_tool(llvm-lsp
  llvm-lsp.cpp
)
