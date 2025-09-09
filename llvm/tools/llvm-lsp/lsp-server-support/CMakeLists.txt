set(LLVM_LINK_COMPONENTS
    Core
    Support
    )

add_llvm_library(lsp-server-support
  STATIC
  CompilationDatabase.cpp
  SourceMgrUtils.cpp
  )
