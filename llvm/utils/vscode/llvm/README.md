# LLVM IR Visualizer VSCode Extension

## Build & Run

```bash
npm install
npm run compile
```

In VSCode you can also just open the `package.json`, find the `scripts` member, and click the little inline `Debug` button.
This will open a command selection and you can select which to run.

After that, navigate to `src/extension.ts` and press `F5` (default binding for `Debug: Start Debugging`) and that should open a new VSCode window which should have `[Extension Development Host]` in its title.

In the new window, navigate to the Output pane (`Ctrl+Shift+U`) and select `llvm-lsp-server` from the dropdown menu. You should see the LSP communication.
If not, check the setting `llvm.trace.server` - should be set to `messages` or `verbose`.

## Custom LSP messages

### `llvm/getCfg`

Client opens a .ll file and wants to open the CFG in a webview (for now custom command).
Server checks if the svg exists, if not, generates it, and returns its uri.

Request:
```
{
  "method": "llvm/getCfg",
  "params": {
    "uri": "file:///path/to/ir.ll",
    "position": Position{.line: 0, .character: 0}
  }
}
```

Response:
```
{
  "result": {
    "uri": "file:///path/to/ir.svg",
    "node_id": "node1",
    "function": "main",
  }
}
```

### `llvm/cfgNode`

Note: probably will instead of the request just use [code actions](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_codeAction).

User clicks on a location in .ll file and runs a command to navigate to the cfg block.
Server has to generate the svg if it doesn't exist, find which basic block the location belongs to, find which node id corresponds to that basic block, and return the node id.
Client has to center the view on the right basic block.

Request:
```
{
  "method": "llvm/cfgNode",
  "params": {
    "uri": "file:///path/to/ir.ll",
    "position": Position{.line: 0, .character: 0}
  }
}

```
Response:
```
{
  "result": {
    "uri": "file:///path/to/ir.svg"
    "node_id": "node1",
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
    "uri": "file:///path/to/ir.svg"
    "node_id": "node1",
  }
}
```

Response:
```
{
  "result": {
    "uri": "file:///path/to/ir.ll",
    "range": Range{.start: Position{.line: 0, .character: 0}, .end: Position{.line: 0, .character: 0}}
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
