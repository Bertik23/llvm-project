import * as vscode from 'vscode';
import * as path from 'path';
import { Command } from './command';
import { LLVMContext } from './llvmContext';
import { RequestType, integer, uinteger } from 'vscode-languageclient';

export namespace LlvmGetCfg {
  export interface Params {
    uri: string;
  }
  export interface Response {
    uri: string;
  }
  export const Type = new RequestType<Params, Response, void>('llvm/getCfg');
}

export namespace LlvmBbLocation {
  export interface Params {
    uri: string;
    node_id: string;
  }
  export interface Response {
    uri: string;
    from_line: uinteger;
    from_col: uinteger;
    to_line: uinteger;
    to_col: uinteger;
  }
  export const Type = new RequestType<Params, Response, void>('llvm/bbLocation');
}

export namespace LlvmGetPassList {
  export interface Params {
    uri: string;
  }
  export interface Response {
    list: [string];
    descriptions: [string];
    status: 'success' | 'error';
  }
  export const Type = new RequestType<Params, Response, void>('llvm/getPassList');
}

export namespace LlvmGetIRAfterPass {
  export interface Params {
    uri: string;
    passnumber: integer;
  }
  export interface Response {
    uri: string;
    status: 'success' | 'error';
  }
  export const Type = new RequestType<Params, Response, void>('llvm/getIRAfterPass');
}

export class LLVMCfgCommand extends Command {

  constructor(context: LLVMContext) {
    super('llvm.cfg', context);
  }

  async execute(...args: any[]) {
    // Only works when there is an active open editor with a .ll file
    const activeEditor = vscode.window.activeTextEditor;
    if (!activeEditor) {
      vscode.window.showInformationMessage('No active text editor.');
      return;
    }
    const currentFileLanguageId = activeEditor.document.languageId;
    if (currentFileLanguageId !== 'llvm') {
      vscode.window.showInformationMessage('Only supported for language `llvm\'.');
      return;
    }
    const currentFileUri = activeEditor.document.uri;
    const client = await this.context.getOrActivateLanguageClient(currentFileUri, currentFileLanguageId);
    if (!client || !client.initializeResult) {
      vscode.window.showErrorMessage('Language server is not yet ready.');
      return;
    }

    // Ask lsp server
    let result: LlvmGetCfg.Response = undefined;
    try {
      const params: LlvmGetCfg.Params = {
        uri: currentFileUri.toString(), // TODO: should we send uri.fspath instead?
      };
      this.context.outputChannel.appendLine('>>>>> ' + currentFileUri.toString());
      const response = await client.sendRequest(LlvmGetCfg.Type, params);
      // TODO: should check if the IDs match??
      if (response['error'] !== undefined) {
        this.context.outputChannel.appendLine(`Error during custom request LlvmGetCfg: server returned error`);
        return;
      }
      result = response['result'];
    } catch (error) {
      this.context.outputChannel.appendLine(`Error during custom request LlvmGetCfg: ${error}`);
      return;
    }

    // Read the cfg from the server's response
    const cfgFilePath = vscode.Uri.file(result['uri']).fsPath;
    const cfgDir = path.dirname(cfgFilePath);
    let targetFileContent: string;
    try {
      const targetUri = vscode.Uri.file(cfgFilePath);
      const fileBytes = await vscode.workspace.fs.readFile(targetUri);
      targetFileContent = Buffer.from(fileBytes).toString('utf8');
    } catch (error) {
      this.context.outputChannel.appendLine(`Could not read file: ${cfgFilePath}. Error: ${error}`);
      return;
    }

    // Create the webview panel and show the svg in it
    const panel = vscode.window.createWebviewPanel(
      'embeddedCFGView',
      'CFG',
      vscode.ViewColumn.Beside,
      {
        enableScripts: true,
        localResourceRoots: [vscode.Uri.file(cfgDir)]
      }
    );
    const nodeToCenter = "node1"; // TODO: add this to the response
    panel.webview.html = await getWebviewContentWithInteraction(this.context, { svgContent: targetFileContent, fileName: path.basename(cfgFilePath), initialNodeToCenter: nodeToCenter });

    // Handle messages from the webview
    this.context.subscriptions.push(
      panel.webview.onDidReceiveMessage(
        async message => {
          switch (message.command) {
            case 'svgElementClicked':
              const elementId = message.elementId;
              vscode.window.showInformationMessage(`SVG Element Clicked: ID = ${elementId}`);

              let result: LlvmBbLocation.Response = undefined;
              try {
                const params: LlvmBbLocation.Params = {
                  uri: currentFileUri.toString(),
                  node_id: elementId,
                };
                this.context.outputChannel.appendLine('>>>>> ' + currentFileUri.toString());
                const response = await client.sendRequest(LlvmBbLocation.Type, params);
                // TODO: should check if the IDs match??
                if (response['error'] !== undefined) {
                  this.context.outputChannel.appendLine(`Error during custom request LlvmBbLocation: server returned error`);
                  return;
                }
                result = response['result'];
              } catch (error) {
                this.context.outputChannel.appendLine(`Error during custom request LlvmGetCfg: ${error}`);
                return;
              }

              const targetUri = vscode.Uri.parse(result['uri']);
              const selection = new vscode.Range(
                new vscode.Position(result['from_line'], result['from_col']),
                new vscode.Position(result['to_line'], result['to_col']));
              const targetEditor = vscode.window.visibleTextEditors.find(editor => {
                return editor.document.uri.toString() === targetUri.toString();
              });
              if (targetEditor) {
                await vscode.window.showTextDocument(targetEditor.document, {
                  viewColumn: targetEditor.viewColumn,
                  preserveFocus: false,
                  selection: selection
                });
                targetEditor.revealRange(selection, vscode.TextEditorRevealType.InCenter);
              } else {
                const document = await vscode.workspace.openTextDocument(targetUri);
                await vscode.window.showTextDocument(document, {
                  selection: selection,
                  viewColumn: vscode.ViewColumn.Beside,
                  preserveFocus: false,
                  preview: false
                });
              }

              this.context.outputChannel.appendLine(`Navigated to: ${targetUri.fsPath}`);
              return;
          }
        },
        undefined,
        this.context.subscriptions
      ));
  }
}

// TODO: Move to different file
export class LLVMGetIRCommand extends Command {

  constructor(context: LLVMContext) {
    super('llvm.get_ir', context);
  }

  async execute(...args: any[]) {
    // Only works when there is an active open editor with a .ll file
    const activeEditor = vscode.window.activeTextEditor;
    if (!activeEditor) {
      vscode.window.showInformationMessage('No active text editor.');
      return;
    }
    const currentFileLanguageId = activeEditor.document.languageId;
    if (currentFileLanguageId !== 'llvm') {
      vscode.window.showInformationMessage('Only supported for language `llvm\'.');
      return;
    }
    const currentFileUri = activeEditor.document.uri;
    const client = await this.context.getOrActivateLanguageClient(currentFileUri, currentFileLanguageId);
    if (!client || !client.initializeResult) {
      vscode.window.showErrorMessage('Language server is not yet ready.');
      return;
    }

    // Ask lsp server
    let result1: LlvmGetPassList.Response = undefined;
    try {
      const params: LlvmGetPassList.Params = {
        uri: currentFileUri.toString(),
      };

      const response = await client.sendRequest(LlvmGetPassList.Type, params);
      result1 = response;
    } catch (error) {
      this.context.outputChannel.appendLine(`Error during custom request LlvmGetPassList: ${error}`);
      return;
    }

    // Parse Result to extract out the PassList and Pass Description
    const passNames = result1['list'];
    const passDescriptions = result1['descriptions'];

    const passItems: vscode.QuickPickItem[] = passNames.map((name, idx) => ({
      label: name,
      description: passDescriptions[idx] || ''
    }));

    const selected = await vscode.window.showQuickPick(passItems, {
      placeHolder: 'Select an LLVM pass to view IR after it'
    });

    if (!selected) {
      // User cancelled
      return;
    }

    const selectedPass = selected.label;
    const passNumMatch = selectedPass.match(/^(\d+)-/);
    const passNum = passNumMatch ? parseInt(passNumMatch[1]) : 0;
    this.context.outputChannel.appendLine(`You selected Pass: ${selectedPass} and PassNumber ${passNum}`);

    // Query server for filepath
    let result2: LlvmGetIRAfterPass.Response = undefined;
    try {
      const params: LlvmGetIRAfterPass.Params = {
        uri: currentFileUri.toString(),
        passnumber: passNum
      };

      const response = await client.sendRequest(LlvmGetIRAfterPass.Type, params);
      result2 = response;
    } catch (error) {
      this.context.outputChannel.appendLine(`Error during custom request LlvmGetPassList: ${error}`);
      return;
    }
    this.context.outputChannel.appendLine(`Received IR File Path: ${result2['uri']}`);
    const cfgFilePath = vscode.Uri.file(result2['uri'].replace(/^file:\/\//, "")).fsPath;

    this.context.outputChannel.appendLine(`Trying to open: ${cfgFilePath}`);
    vscode.workspace.openTextDocument(cfgFilePath).then(doc => {
      vscode.window.showTextDocument(doc);
    });
  }
}

async function getWebviewContentWithInteraction(context: LLVMContext, data: Record<string, string>) {
  const filePath = path.join(context.context.extensionPath, 'templates', 'cfgViewer.html');
  let templateBytes = await vscode.workspace.fs.readFile(vscode.Uri.file(filePath));
  let targetFileContent = Buffer.from(templateBytes).toString('utf8');

  // TODO: safety!
  for (const [key, value] of Object.entries(data)) {
    const placeholder = new RegExp(`\\$\\{${key}\\}`, 'g');
    targetFileContent = targetFileContent.replace(placeholder, value);
  }

  return targetFileContent;
}
