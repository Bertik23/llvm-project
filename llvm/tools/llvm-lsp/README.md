# LLVM LSP server

## Build

```bash
cmake -S llvm -B buildR -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
ninja -j 6 -C buildR llvm-lsp-server
```
Or
```bash
cmake -S llvm -B buildRA -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
ninja -j 6 -C buildRA llvm-lsp-server
```

## Features

This LSP server is built to the Language Server Protocol Specification 3.17. It provides several standard and custom features to enhance the development experience.

---

### Standard Capabilities

The server supports the following standard LSP capabilities:

* `textDocumentSync.openClose`: Synchronizes document content with the server.
* `referencesProvider`: Finds all references to a symbol.
* `codeActionProvider`: Provides code actions, such as quick fixes and refactorings. This server uses it to provide CFG views.

---

### Custom Methods

In addition to the standard capabilities, the server exposes several custom methods tailored for LLVM development.

#### `llvm/getCfg`

This method generates and returns an SVG representation of the Control Flow Graph (CFG) for the function at a specified position.

**Parameters**

```ts
interface GetCfgParams {
    /**
     * The URI of the file for which the CFG is requested.
     */
    uri: string;
    /**
     * The cursor's position. The CFG is generated for the function where the cursor is located.
     */
    position: Position;
}
```

**Response**

```ts
interface CFG {
    /**
     * URI of the SVG file containing the CFG.
     */
    uri: string;
    /**
     * The ID of the node corresponding to the basic block where the cursor was located.
     */
    node_id: string;
    /**
     * The name of the function for which the CFG was generated.
     */
    function: string;
}
```

---

#### `llvm/bbLocation`

This method retrieves the location of a basic block within the source code, identified by its node ID from a generated CFG.

**Parameters**

```ts
interface BbLocationParams {
    /**
     * The URI of the SVG file containing the CFG.
     */
    uri: string;
    /**
     * The ID of the node representing the basic block.
     */
    node_id: string;
}
```

**Response**

```ts
interface BbLocation {
    /**
     * The URI of the `.ll` file containing the basic block.
     */
    uri: string;
    /**
     * The range of the basic block corresponding to the node ID.
     */
    range: Range;
}
```

---

#### `llvm/getPassList`

This method returns a list of optimization passes that would be applied by a given optimization pipeline.

**Parameters**

```ts
interface GetPassListParams {
    /**
     * The URI of the `.ll` file for which the pass list is requested.
     */
    uri: string;
    /**
     * The optimization pipeline string, in the format passed to the `opt` tool.
     */
    pipeline: string;
}
```

**Response**

```ts
interface PassList {
    /**
     * A list of passes in the pipeline, formatted as `<number>-<name>`.
     */
    list: string[];
    /**
     * A list of descriptions corresponding to each pass.
     */
    descriptions: string[];
    /**
     * A status indicator for the request.
     */
    status: string = "success";
}
```

---

#### `llvm/getIRBeforePass`

This method retrieves the Intermediate Representation (IR) of the code after a specific optimization pass in a pipeline has been applied.

**Parameters**

```ts
interface GetIRBeforePassParams {
    /**
     * The URI of the `.ll` file for which the intermediate IR is requested.
     */
    uri: string;
    /**
     * The optimization pipeline string, in the format passed to the `opt` tool.
     */
    pipeline: string;
    /**
     * The number of the pass in the pipeline before which to return the IR.
     */
    passnumber: uinteger;
    /**
     * Additional arguments passed to opt.
     */
    additional_opt_args: string[]?;
}
```

**Response**

```ts
interface IR {
    /**
     * The URI of the `.ll` file containing the generated intermediate IR.
     */
    uri: string;
}
```


# Developer documentation

## Core ideas

### Separation of responsibilities
The LSP server is responsible for parsing LLVM IR, analyzing it, and interfacing with LLVM tools like `opt`. It exposes this functionality through the Language Server Protocol. The client (e.g., VS Code extension) is responsible for the UI and visualization.

### Artifact Management
The server generates various artifacts to support features like CFG visualization and optimization pipeline inspection.
- **Storage**: Generated files (e.g., `.dot`, `.svg`, intermediate `.ll`) are stored in a directory named `Artifacts-<SourceFileName>` located next to the original source file.
- **Caching**: The `IRArtifacts` class manages these files, aiming to reuse them when possible to improve performance.

### Optimization Integration
The server integrates with `opt` to provide insights into the optimization pipeline.
- **Pass Lists**: Can retrieve the list of passes in a pipeline using `opt --print-pass-numbers`.
- **Intermediate IR**: Can generate IR content after specific passes using `opt --print-before-pass-number`.

### Tracking Value Source Location
The server tracks the location of LLVM IR values (instructions, globals, etc.) relative to the text document using the `AsmParserContext`.
- **Mechanism**: The `IRDocument` class is responsible for maintaining the state of an open file. It holds the parsed LLVM `Module` and the `AsmParserContext`.
- **Usage**: When the `llvm-lsp-server` parses the `.ll` file, the `AsmParserContext` captures location information (line and column numbers). When an LSP request comes in with a specific text cursor position, the server queries this context to map the text position back to the specific LLVM `Value*` or `Instruction*` using `getValueReferencedAtLocation`. It also supports reverse mapping via `getInstructionOrArgumentLocation`.

### Document Lifecycle
The lifecycle of a document is managed by the `LspServer` class, interacting with `IRDocument` and `IRArtifacts`.
1. **Creation (Open)**: Triggered by `textDocument/didOpen`. The `LspServer` creates a new `IRDocument`. The source is parsed into an LLVM Module, and `AsmParserContext` is populated.
2. **Synchronization (Update)**: Triggered by `textDocument/didChange`. The server keeps the document content in sync. `IRDocument` parses the updated IR.
3. **Artifact Management**: During the document's life, `IRArtifacts` manages generated files (like `.dot`, `.svg`, or intermediate `.ll` files) in a directory named `Artifacts-<SourceFileName>`.
4. **Destruction (Close)**: Triggered by `textDocument/didClose`. The `LspServer` destroys the `IRDocument` instance.

**Handling Files with Errors**
The server handles parsing errors as follows:
1. **Detection**: Upon opening or changing a file, the server attempts to parse the content.
2. **Reporting**: If parsing fails, the `IRDocument` captures the error (via `SMDiagnostic`). This is converted to an LSP `Diagnostic` and sent to the client to underline the error location.
3. **Isolation**: Files with errors are removed from the active documents list (`OpenDocuments`). This ensures that subsequent requests (which require a valid Module) do not crash or operate on invalid state.

## Debugging notes

### LSP message does nothing

When a request/notification appears to have no effect, check these first:

1. **Handler registration**
    - Verify the method is registered in `LspServer::registerMessageHandlers()` in [llvm-lsp-server.cpp](llvm-lsp-server.cpp).
    - If it is missing there, the server will never dispatch incoming messages to your handler.

2. **Capability advertisement**
    - Verify `handleRequestInitialize()` advertises the corresponding capability in the `capabilities` object.
    - If a capability is not advertised, many clients will not send that message.

### Quick checklist

- Confirm method name strings match exactly between client and server (for example, `llvm/getPassList`).
- Confirm request/notification type matches registration (`method(...)` vs `notification(...)`).
- Confirm the target document is in `OpenDocuments` when the handler runs.
- Check the server log (default: `/tmp/llvm-lsp-server.log`) for handler entry messages and early returns.

### Deep checks (useful for tricky failures)

- **Initialization gating**: verify the client actually consumed the server's `initialize` response and is not filtering features based on missing/incorrect capability shape.
- **JSON schema mismatch**: for custom methods, validate `fromJSON(...)` mappings in `Protocol.cpp` against the exact client payload. A missing required field can make requests fail before business logic runs.
- **Request vs notification semantics**: if a client is waiting for a result, ensure the server registered a `method` (not `notification`) and always reaches `Reply(...)` on all control-flow paths.
- **Document lifecycle race**: failed parse in `didOpen`/`didChange` removes the file from `OpenDocuments`; later requests for that URI will appear to do nothing except early-return.
- **Coordinate system bugs**: keep line/column conventions consistent end-to-end (LSP and `FileLoc` in this project are 0-based). Off-by-one errors can silently target the wrong IR object.
- **Guard-condition opacity**: many handlers intentionally return empty results for invalid context (no symbol under cursor, no function at location, etc.). Add temporary debug logs immediately before each early return when diagnosing.
- **Tooling/path issues**: for pass-related features, verify `opt` discovery (`--opt-path` or PATH) and inspect stderr from failed runs; execution errors often surface as empty/failed responses.
- **Artifact cache confusion**: stale files in `Artifacts-<SourceFileName>` can mask current behavior while debugging generation issues. Clear artifacts when validating pipeline changes.
- **Different results between cli opt and running opt from lsp**: The first argument in the argument list of `llvm::ExecuteAndWait` is arg0, so the program binary.

## Project Structure

### `llvm-lsp-server.cpp` / `.h`
- **`LspServer`**: The main class that orchestrates the server. It initializes the transport, manages the lifecycle, and dispatches incoming LSP messages to specific handlers.
- **Handlers**: Methods like `handleRequestGetCFG` or `handleRequestHover` containing the logic for each feature.

### `IRDocument.h`
- **`IRDocument`**: Represents an open LLVM IR document. It maintains the parsed module and the `AsmParserContext` used for location queries.
- **`IRArtifacts`**: Manages the directory of generated artifacts, handling the creation of CFG graphs (Dot/SVG) and storage of intermediate IRs.

### `OptRunner.h`
- **`OptRunner`**: A wrapper around the `opt` tool. It constructs command lines for running passes or querying pass lists and parses the output.
- **Purpose**: The `OptRunner` class encapsulates the interaction with the LLVM `opt` command-line tool. Its primary purpose is to abstract away the complexities of invoking `opt` with various arguments, handling temporary files for input/output, and parsing `opt`'s stderr/stdout to extract useful information (like pass lists or intermediate IR).
- **Usage**: `OptRunner` instances are typically created via `OptRunnerFactory` within `IRDocument`. This factory allows `OptRunner` to inherit or override default `opt` arguments (e.g., `pipeline-opt-args`, `pass-opt-args`) from an `IrOptArgs` JSON file, enabling persistent configuration for optimization runs. It's used to:
    - Retrieve a list of available passes and their descriptions (`getPassListAndDescription`).
    - Generate intermediate IR after a specific pass in a pipeline (`getModuleBeforePass`).
    - Run a specific pass on the current IR (`runPass`).

    **Argument Types**:
    - **Pipeline Arguments (`pipeline-opt-args`)**: "Hereditary" arguments that persist across generated IR files. When an IR file is generated, these arguments are saved in `ir_opt_args.json`. When that IR is loaded, these arguments are restored, overriding any pipeline arguments passed in the request. This ensures consistent configuration (e.g. data layout) throughout the optimization chain.
    - **Pass Arguments (`pass-opt-args`)**: Transient arguments specific to a single optimization request. They are used for the current execution but are not inherited by subsequent steps.

### `Protocol.h` / `.cpp`
- Definitions of structs corresponding to the JSON parameters and responses for both standard and custom LSP methods.
- Serialization logic (`fromJSON`/`toJSON`).
