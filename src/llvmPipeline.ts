import * as vscode from 'vscode';
import { Command } from './command';
import { LLVMContext } from './llvmContext';
import {
  LlvmGetPassList,
  LlvmGetIRAfterPass,
} from './lspCustomMessages';

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
    const pipeline = await vscode.window.showInputBox({
      prompt: 'Optimization pipeline',
      placeHolder: 'default<O3>',
    });
    try {
      const params: LlvmGetPassList.Params = {
        uri: currentFileUri.toString(),
        pipeline: pipeline
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
        passnumber: passNum,
        pipeline: pipeline
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
