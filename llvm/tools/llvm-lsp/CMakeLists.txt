set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  Support
  Analysis
  Passes
  SupportLSP
  TransformUtils
  AsmParser
)

add_llvm_tool(llvm-lsp-server
  llvm-lsp-server.cpp
  Protocol.cpp
)

target_compile_features(llvm-lsp-server PRIVATE cxx_std_23)

# Disable precompiled headers for this project because otherwise the above
# setting breaks Darwin build.
set_source_files_properties(llvm-lsp-server.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(Protocol.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
