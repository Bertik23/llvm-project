set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  Support
  Analysis
  Passes
)

add_llvm_tool(llvm-lsp-server
  llvm-lsp-server.cpp
  Protocol.cpp
)

add_subdirectory(lsp-server-support)
target_link_libraries(llvm-lsp-server PUBLIC lsp-server-support)
target_compile_features(llvm-lsp-server PRIVATE cxx_std_23)
