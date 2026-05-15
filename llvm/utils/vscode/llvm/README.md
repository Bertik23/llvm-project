# LLVM Development VS Code Extension

This VS Code extension provides a comprehensive suite of tools for working with LLVM projects. It includes syntax highlighting, LIT integration, and a custom LLVM IR visualizer with LSP-backed commands.

---

## Features

### Syntax Highlighting

- **LLVM IR (.ll)** — syntax highlighting translated from `llvm/utils/vim/syntax/llvm.vim`
- **TableGen (.td)** — syntax highlighting from `llvm/utils/textmate`

### LIT Test Integration

- Pattern matchers for LIT test output (`$llvm-lit`, `$llvm-filecheck`)
- VS Code Tasks to run LIT on the current file:
  - `Terminal` → `Run Task` → `llvm-lit`

### LLVM IR Visualizer

- Integrated LSP-based webview visualization of CFGs
- Navigation between IR and CFG nodes
- Running of optimization pipelines and retrieving IR after arbitrary pass
- Supports custom LSP messages:
  - `llvm/getCfg` — view CFG as SVG
  - `llvm/bbLocation` — jump to IR location from CFG node
  - `llvm/getPassList` — get list of optimization passes
  - `llvm/getIRAfterPass` — run a optimization pipeline and return IR after specified pass

---

## Installation

### Prerequisites

```bash
sudo apt-get install nodejs-dev node-gyp npm
sudo npm install -g typescript npx vsce
```

### Install From Source

```bash
cd <extensions-installation-folder>
cp -r llvm/utils/vscode/llvm .
cd llvm
npm install
npm run vscode:prepublish
```

📌 `<extensions-installation-folder>` is OS dependent. See:  
https://code.visualstudio.com/docs/editor/extension-gallery#_where-are-extensions-installed

### Install From Package (.vsix)

1. Package the extension:  
   https://code.visualstudio.com/api/working-with-extensions/publishing-extension#usage
2. Install the `.vsix`:  
   https://code.visualstudio.com/docs/editor/extension-gallery#_install-from-a-vsix

---

## Setup

Set the following in your VS Code settings:

```json
"cmake.buildDirectory": "<your-cmake-build-dir>",
"llvm.installPath": "<path-to-llvm-install-root>"
```

If `"llvm.installPath"` is not set, the extension will search for LLVM tools in your system `PATH`.

Resources:

- [VS Code User Settings](https://code.visualstudio.com/docs/getstarted/settings)
- [CMake Tools: buildDirectory](https://vector-of-bool.github.io/docs/vscode-cmake-tools/settings.html#cmake-builddirectory)

---

## Development

### Build & Debug

```bash
npm install
npm run compile
```

Alternatively:

1. Open `package.json` in VS Code.
2. Click the `Debug` button next to any script under the `scripts` section.
3. Open `src/extension.ts`, press `F5` (Debug: Start Debugging).
4. A new window titled `[Extension Development Host]` will launch.

### Debugging LSP Communication

In the Extension Development Host:

- Open the **Output** pane (`Ctrl+Shift+U`)
- Select `llvm-lsp-server` from the dropdown
- Make sure the setting `llvm.trace.server` is set to `"messages"` or `"verbose"`

---

## Usage

### Viewing the Control Flow Graph (CFG)

#### Open a CFG for a Function.

1. Place your cursor inside the function you want to visualize.
2. A yellow lightbulb icon will appear in the gutter (to the left of the line numbers).
3. Click the lightbulb and select **Open CFG view**.
4. The CFG view will open. If a CFG view for this function is already open, it will be brought into focus.
5. The view will automatically center on the basic block where your cursor is located.

#### Highlighting Basic Blocks

1. In the CFG view, click on any empty space in a basic block to highlight it in the source editor.
2. The editor will reveal and select the corresponding block. If the file is not already open, it will be opened.

#### Navigating the CFG view

**Search:** Use the search bar at the top to highlight and iterate through all matching results.

**Move:** Hold <kbd>Ctrl</kbd> and drag to move around the view.

**Zoom:** Hold <kbd>Ctrl</kbd> and scroll to zoom in or out.

### Running Optimization Passes

#### Get Intermediate IR

1. Open the Command Palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>).
2. Run the command: **Get Intermediate IR**.
3. Choose from previously used optimization pipelines or add a new one.
   - If adding a new pipeline use the same format as you would pass to `opt`
   - To remove a pass from the selector, instead of selecting a pipeline, select `Delete saved inputs` and then the pipelines to delete.
4. After selecting a pipeline, you will see a list of passes in that pipeline.
5. Select the pass after which you want to view the IR. The resulting IR will open in a new editor tab.

#### Inspecting Further Passes

- To continue exploring the pipeline, return to the original file and select another pass.
- Note: The entire pipeline is always run from the original IR. Running it on an intermediate IR may produce unexpected results.

---

All generated files, including `.dot` and `.svg` files for the CFGs and the generated IR, are stored in a new directory. This directory is named `Artifacts-<ll file name>` and is located next to the original `.ll` file.

## Custom LSP Messages

This VS Code extension supports a number of custom LSP messages. The specification of those messages can be found in `src/lspCustomMessages.ts`.

These messages are implemented by the LLVM LSP server.

---

# Developer documentation

## Core ideas

### Separation of responsibilities

The VS Code extension handles the visualization of the project. Generally, all file structure and deep language logic should be handled by the LSP server.

Rule of Thumb for placement:

- Use the VS Code Extension for: UI, visualization, and running external tools (other than opt). If a task is purely client-side or tool-orchestration, do it here.
- Use the LSP Server for: Any information regarding the IR, file structures, or deep code analysis.

### Backwards compatibility

The VS Code extension should be generally backwards compatible with previous LSP servers. Additional fields in messages are not a problem and missing fields will be usually optional.

### CFG view

The main long term idea of the CFG view is:

1. LSP server generates JSON for LLVM IR / object files
2. VS Code extension passes this JSON to the CFG viewer.

### Multi-Root Workspace Support

The extension is built to support VS Code's multi-root workspace feature.

- **`LLVMContext`**: Manages the overall extension lifecycle.
- **`WorkspaceFolderContext`**: Created for each workspace folder to maintain an independent `LanguageClient` instance, ensuring isolation of build configurations and server states.

### Task Provider Integration

Instead of relying solely on ad-hoc commands, the extension integrates with the VS Code Task system via `LITTaskProvider`.

- This enables the `llvm-lit` task detection.
- It allows users to run and configure tests using the standard "Run Task" UI and `tasks.json`.
- This is the original VS Code LLVM extension

### Artifact Management

To avoid cluttering the source directories with intermediate files produced by tools like `opt` or `llvm-objdump` (e.g., annotated IR, bitcode dumps):

- **Isolation**: Generated files are stored in a specific subdirectory named `Artifacts-<SourceFileName>` located next to the source file.
- **Lifecycle**: These artifacts are generated on-demand by commands like `llvm.get_annotated_ir`.

### Webview Communication

The CFG View operates as a separate Webview context, isolated from the main extension process.

- **Message Passing**: Communication handles synchronization between the editor cursor and the visual graph.
- **Protocol**:
  - **Extension → Webview**: Sends `showLine` (to highlight blocks) or `showFunction` (to switch graphs).
  - **Webview → Extension**: Sends `goto` (to jump to source code) or `isReady`.

## Debugging extension + LSP

### Debugging workflow

- Start the extension in debug mode (`F5`) and use the `[Extension Development Host]` window for testing.
- The default `Run Extension` launch config runs `npm: watch` as a pre-launch task and debugs code from `out/**/*.js`.
- Use command `llvm.restart` after changing extension-side activation/state logic to restart language clients cleanly.
- Keep `llvm.trace.server` set to `messages` or `verbose` while debugging LSP traffic.

### Output channels to watch

- Open **View → Output** (`Ctrl+Shift+U`) in the Extension Development Host.
- Select `llvm-lsp-server` in the output channel dropdown to inspect:
  - Language client / server logs.
  - Extension-side diagnostics written through `LLVMContext.outputChannel` (including CFG request/message flow).

### Webview debugging (CFG viewer)

- For the extension host process, run **Developer: Toggle Developer Tools**.
- For the webview itself, run **Developer: Open Webview Developer Tools**.
- Use those tools to inspect console logs, network/resource loading, and message traffic between webview and extension.

### Troubleshooting checklist

- **Changed code, but nothing changed when running**
  - This extension runs from transpiled files in `out/`, not directly from `src/`.
  - Most common cause: no successful build happened after edits (`npm run compile` not run, or failed with errors).
  - In debug mode, `F5` starts `npm run watch`; if watch/tsc reports errors, stale JS in `out/` is used, so your change will not appear.
  - Fix: check the `npm: watch` / `npm: compile` task output, resolve all TypeScript errors, wait for a successful build, then re-run `F5` (or reload the Extension Development Host).

- **No logs in Output**
  - Confirm you are in the `[Extension Development Host]` window, not the main VS Code window.
  - Open **View → Output** and explicitly select the `llvm-lsp-server` channel.
  - Run `llvm.restart`, then trigger an LLVM action (for example, open a `.ll` file and run **Open CFG view**).

- **Server not starting**
  - Verify `llvm.installPath` points to an LLVM install root containing `bin/llvm-lsp-server` and `bin/opt`.
  - Check `cmake.buildDirectory` is valid and `opt` is available under `<buildDirectory>/bin`.
  - Look for startup errors in `llvm-lsp-server` output and fix path/config issues first.

- **Webview is blank or not updating**
  - Open **Developer: Open Webview Developer Tools** and inspect console errors.
  - Re-run **Open CFG view** from a valid LLVM function and confirm the generated CFG artifact exists.
  - If changes are not reflected, close/reopen the CFG panel or run `llvm.restart` to reset extension/webview state.

## Project Structure

### `package.json`

Metadata and configuration:

- Extension name, version, engines, activation events, etc.
- Contributions:
  - `languages`, `commands`, `menus`, `configuration`

### Configuration Files

- `language-configuration.json`: Language configuration for LLVM IR.
- `language-configuration-tablegen.json`: Language configuration for TableGen.

### `syntaxes/`

- `ll.tmLanguage.json`: TextMate grammar for LLVM IR syntax highlighting.
- `TableGen.tmLanguage`: TextMate grammar for TableGen syntax highlighting.

### `src/` — TypeScript sources

- `extension.ts`
  - Entry point: creates `OutputChannel`, `LLVMContext`, and registers commands
- `llvmContext.ts`
  - `WorkspaceFolderContext`: manages `LanguageClient` per workspace
  - `LLVMContext`: manages lifecycle, subscriptions, and per-folder context
- `autoUpdater.ts`
  - Handles extension auto-updates from Nexus.
- `orcaAnnotatedIr.ts`
  - Implementation of the Annotated IR feature.
- `lspCustomMessages.ts`
  - Definitions for custom LSP messages (CFG, Pipeline, etc.).
- `litTaskProvider.ts`
  - Provides task definitions for running LIT tests.
- `llvmCfg.ts`
  - Commands for generating and managing the CFG visualization.
- `llvmPipeline.ts`
  - Commands for retrieving IR at different pipeline stages.
- `llvmPass.ts`
  - Handles running specific optimization passes.
- `objdump.ts`
  - Interface for running `llvm-objdump` and decorating the editor.
- `filePicker.ts`
  - Utilities for file selection within the extension.

### `cfg_webview/` - Source files for CFG view

- `main.ts`
  - Entry point, creates the webview and handles massages.

## Afiliation

<img src="https://fit.cvut.cz/static/images/fit-cvut-logo-en.svg" alt="FIT CTU logo" height="200">

This software was developed with the support of the **Faculty of Information Technology, Czech Technical University in Prague**.
For more information, visit [fit.cvut.cz](https://fit.cvut.cz).
