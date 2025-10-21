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

add_llvm_unittest(LspTests
    OptRunner.cpp
  )

target_link_libraries(LspTests PRIVATE LLVMTestingSupport)
