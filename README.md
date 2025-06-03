# LLVM IR Visualizer VSCode Extension

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
