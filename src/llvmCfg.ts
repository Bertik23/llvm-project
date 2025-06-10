import * as vscode from 'vscode';
import * as path from 'path';
import { Command } from './command';
import { LLVMContext } from './llvmContext';
import {
  LlvmGetCfg,
  LlvmBbLocation,
} from './lspCustomMessages';

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
    // TODO: should we send uri.fspath instead? what's the system-agnostic way to pass uri?
    //   In general, I think we should use vscode.Uri everywhere instead of strings (the context maps etc.)
    let result: LlvmGetCfg.Response = undefined;
    try {
      const params: LlvmGetCfg.Params = {
        uri: currentFileUri.toString(),
        position: activeEditor.selection.active,
      };
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
    const cfgFileUri = vscode.Uri.parse(result['uri']);
    const cfgFilePath = cfgFileUri.fsPath;
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
      'embeddedView',
      `CFG for ${result['function']} from ${path.basename(currentFileUri.fsPath)}`,
      vscode.ViewColumn.Beside,
      {
        enableScripts: true,
        localResourceRoots: [vscode.Uri.file(cfgDir)]
      }
    );
    const nodeToCenter = result['node_id'];
    panel.webview.html = await getWebviewContentWithInteraction(
      this.context,
      {
        svgContent: targetFileContent,
        fileName: cfgFilePath,
        initialNodeToCenter: nodeToCenter
      });

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
                  uri: cfgFileUri.toString(),
                  node_id: elementId,
                };
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
              // can I have just this since we send the right shape?
              // const selection = result['range'];
              const selection = new vscode.Range(
                new vscode.Position(Math.max(0, result['range']['start']['line']), Math.max(0, result['range']['start']['character'])),
                new vscode.Position(Math.max(0, result['range']['end']['line']), Math.max(0, result['range']['end']['character'])));
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
