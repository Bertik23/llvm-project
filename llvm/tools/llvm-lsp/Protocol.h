#include "lsp-server-support/Protocol.h"

// This file is using the LSP syntax for identifier names which is different
// from the LLVM coding standard. To avoid the clang-tidy warnings, we're
// disabling one check here.
// NOLINTBEGIN(readability-identifier-naming)

namespace llvm {
namespace lsp {
struct GetCfgParams {
  URIForFile uri;
  Position position;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, GetCfgParams &Result,
              llvm::json::Path Path);

struct CFG {
  URIForFile uri;
  std::string node_id;
  std::string function;
};

llvm::json::Value toJSON(const CFG &Value);

struct BbLocationParams {
  /**
   * The URI of the SVG file containing the CFG.
   */
  URIForFile uri;
  /**
   * The ID of the node representing the basic block.
   */
  std::string node_id;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, BbLocationParams &Result,
              llvm::json::Path Path);

struct BbLocation {
  /**
   * The URI of the `.ll` file containing the basic block.
   */
  URIForFile uri;
  /**
   * The range of the basic block corresponding to the node ID.
   */
  Range range;
};

llvm::json::Value toJSON(const BbLocation &Value);

} // namespace lsp
} // namespace llvm
// NOLINTEND(readability-identifier-naming)
