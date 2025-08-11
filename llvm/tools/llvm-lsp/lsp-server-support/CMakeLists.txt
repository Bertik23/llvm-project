set(LLVM_LINK_COMPONENTS
    Core
    Support
    )

add_llvm_library(lsp-server-support
  STATIC
  CompilationDatabase.cpp
  Logging.cpp
  Protocol.cpp
  SourceMgrUtils.cpp
  Transport.cpp
  )
