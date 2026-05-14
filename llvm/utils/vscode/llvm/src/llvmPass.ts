import * as vscode from 'vscode';
import * as path from 'path';
import format = require('string-template')
import { Command } from './command';
import { LLVMContext } from './llvmContext';
import {
  LlvmGetPassList,
  LlvmGetIRBeforePass,
  LlvmRunPassOnIR,
} from './lspCustomMessages';

export class LLVMRunPassCommand extends Command {

  constructor(context: LLVMContext) {
    super('llvm.run_pass', context);
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

    // Ask user for pass
    const picked = await vscode.window.showInputBox({
      placeHolder: '',
      title: "Passname, including parameters in <> brackets",
      prompt: "Prompt",
      ignoreFocusOut: true,
    });

    // If user canceled
    if (!picked) return;


    const replacmentVals = {
      filePath: currentFileUri.path,
      fileDir: path.dirname(currentFileUri.path),
      fileName: path.basename(currentFileUri.path),
      fileNameWithoutExtension: path.parse(currentFileUri.path).name
    }

    // Query server for filepath
    let result2: LlvmGetIRBeforePass.Response = undefined;
    try {
      const params: LlvmRunPassOnIR.Params = {
        uri: currentFileUri.toString(),
        pass: picked,
        pipeline_opt_args: vscode.workspace.getConfiguration("llvm").get<string[]>("additionalOptArgs", []).map(str => format(str, replacmentVals)),
        pass_opt_args: (await vscode.window.showInputBox({placeHolder: '', title: 'Pass cli parameters', ignoreFocusOut: true})).split(/[ \n]/).filter(str => str.length > 0)
      };

      const response = await client.sendRequest(LlvmRunPassOnIR.Type, params);
      result2 = response;
    } catch (error) {
      this.context.outputChannel.appendLine(`Error during custom request LlvmRunPassOnIR: ${error}`);
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
