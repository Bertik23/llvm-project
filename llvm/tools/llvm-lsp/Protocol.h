#include "lsp-server-support/Protocol.h"

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

} // namespace lsp
} // namespace llvm
