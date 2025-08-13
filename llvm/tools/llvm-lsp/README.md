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

This LSP server supports ths Language Server Protocol Specification 3.17.

The capabilities of this server are:
- `textDocumentSync.openClose`
- `referencesProvider`
- `codeActionProvider`
- `definitionProvider`

Additionaly to these capabilities the server support also some custom messages.

### llvm/getCfg
Get svg with CFG of the function where that is on a position.

Params
```ts
interface GetCfgParams {
    /**
     * The URI of the file for which the CFG was requestsd
     */
    uri: string;
    /**
     * Position of the cursor, CFG for the function in which the cursor is located is generated.
     */
    position: Position;
}
```
Response
```ts
interface CFG {
    /// URI with the SVG file with the CFG
    uri: string;
    /// Id of the node containing the basic block where the cursor was located
    node_id: string;
    /// Name of the function for which the CFG was generated
    function: string;
}
```

### llvm/bbLocation
Get the location of a basic block, based on the id of a node in the CFG svg.

Params
```ts
interface BbLocationParams {
    /**
     * URI of the SVG file containing the CFG for which this requests is made
     */
    uri: string;
    /**
     * Id of the node containing the basic block
     */
    node_id: string;
}
```
Response
```ts
interface BbLocation {
    /// URI with the `.ll` file with the basic block
    uri: string;
    /// Range of the basic block coresponding to the node id
    range: Range;
}
```

### llvm/getPassList
Get the list of passes in a optimization pipeline

Params
```ts
interface GetPassListParams {
    /**
     * URI of the `.ll` file for which the pass list was requested
     */
    uri: string;
    /**
     * Optimization pipeline as would be passed to `opt`
     */
    pipeline: string;
}
```
Response
```ts
interface PassList {
    /// List of the passes in the pipeline in the form of `<number>-<name>`
    list: string[];
    /// List of descriptions coresponding to the passes
    descriptions: string[];
    /// Status indicator
    status: string = "success";
}
```

### llvm/getIRAfterPass
Get a intermediate IR after a specific pass in a pipeline.

Params
```ts
interface GetIRAfterPassParams {
    /**
     * URI of the `.ll` file for which the intermediate IR was requested
     */
    uri: string;
    /**
     * Optimization pipeline as would be passed to `opt`
     */
    pipeline: string;
    /// Number of the pass in the pipeline to return the IR after
    passnumber: uinteger;
}
```
Response
```ts
interface IR {
    /// URI of the `.ll` file
    uri: string;
}
```
