# LLVM IR Visualizer VSCode Extension

## Custom LSP messages

### `llvm/getCfg`

Client opens a .ll file and wants to open the CFG in a webview (for now custom command).
Server checks if the svg exists, if not, generates it, and returns its uri.

Request:
```
{
  "method": "llvm/getCfg",
  "params": {
    "uri": "file:///path/to/ir.ll"
  }
}
```

Response:
```
{
  "result": {
    "uri": "file:///path/to/ir.svg"
  }
}
```

### `llvm/cfgNode`

User clicks on a location in .ll file and runs a command to navigate to the cfg block.
Server has to generate the svg if it doesn't exist, find which basic block the location belongs to, find which node id corresponds to that basic block, and return the node id.
Client has to center the view on the right basic block.

Request:
```
{
  "method": "llvm/cfgNode",
  "params": {
    "uri": "file:///path/to/ir.ll",
    "line": "a",
    "col": "b"
  }
}

```
Response:
```
{
  "result": {
    "node_id": "node1",
    "uri": "file:///path/to/ir.svg"
  }
}
```

### `llvm/bbLocation`

User clicks on a basic block in the cfg preview.
Server needs to find the basic block that the node id corresponds to and get the location in the .ll file.
Client has to move to the location.

Request:
```
{
  "method": "llvm/bbLocation",
  "params": {
    "node_id": "node1",
    "uri": "file:///path/to/ir.cfg"
  }
}
```

Response:
```
{
  "result": {
    "uri": "file:///path/to/ir.ll",
    "from_line": "a",
    "from_col": "b",
    "to_line": "c",
    "to_col": "d"
  }
}
```

## Notes

`package.json`: metadata about the extension
  - name, version, homepage, categories, keywords...
  - dependencies
  - `engines`: vscode api compatibility (`engines`)
  - `activationEvents`: activates on these being emitted (eg. opening language file, if workspace contains glob pattern...)
  - `main`
  - `scripts`
  - `contributes`: what this extension provides
    - `languages`
    - `configuration`: the settings for the extension
    - `commands`
    - `menus`

`src/`: typescript sources
  - `extension.ts`
    - `activate`: creates `OutputChannel`, `LLVMContext`, registers commands, calls activate on LLVMContext
  - `llvmContext.ts`:
    - `WorkspaceFolderContext`: remembers a `LanguageClient` for each folder in workspace
    - `LLVMContext`: remembers `subscriptions` (for cleanup), a `WorkspaceFolderContext` for each folder, outputChannel
      - `activate`
