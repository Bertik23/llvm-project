import * as vscode from 'vscode';
import * as path from 'path';
import format = require('string-template')
import { Command } from './command';
import { LLVMContext } from './llvmContext';
import {
  LlvmGetPassList,
  LlvmGetIRBeforePass,
} from './lspCustomMessages';
import * as config from './config';

export class LLVMGetIRCommand extends Command {
  private irgetter: IRGetter;

  constructor(context: LLVMContext) {
    super('llvm.get_ir', context);
    this.irgetter = new IRGetter(context);
  }

  async execute(...args: any[]) {
    this.irgetter.get_ir();
  }
}

export class LLVMGetStageCommand extends Command {
  private irgetter: IRGetter;

  constructor(context: LLVMContext) {
    super('llvm.get_stage', context);
    this.irgetter = new IRGetter(context);
  }

  async execute(...args: any[]) {
    this.irgetter.get_ir(vscode.workspace.getConfiguration("llvm").get<string[]>("pipelineStageFilters", []))
  }
}

class IRGetter {
  private context: LLVMContext;

  constructor(context: LLVMContext) {
    this.context = context;
  }

  async get_ir(filter_pases: string[] = [".*"]) {
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

    // Ask user for pipeline
    const predefinedPipelines = vscode.workspace.getConfiguration("llvm").get<string[]>("optimizationPipelines", []).concat(['default<O2>', 'default<O3>']);
    const storedPipelines = this.context.context.globalState.get<string[]>('userPipelines', []);

    const combinedOptions = [...predefinedPipelines, ...storedPipelines, '$(plus) Add new pipeline', '$(trash) Delete saved inputs'];
    const picked = await vscode.window.showQuickPick(combinedOptions, {
      placeHolder: 'Pick a optimization pipeline or add a new one'
    });

    // If user canceled
    if (!picked) return;

    let pipeline;
    if (picked === '$(plus) Add new pipeline') {
      const newInput = await vscode.window.showInputBox({
        prompt: 'Enter your custom pipeline',
      });

      // If user canceled
      if (!newInput)
        return;
      pipeline = newInput;
    } else if (picked === '$(trash) Delete saved inputs') {
      if (storedPipelines.length === 0) {
        vscode.window.showInformationMessage('No saved inputs to manage.');
        return;
      }

      const pipelinesToDelete = await vscode.window.showQuickPick(storedPipelines, {
        placeHolder: 'Select inputs to delete',
        canPickMany: true
      });

      if (pipelinesToDelete && pipelinesToDelete.length > 0) {
        const updatedPipelines = storedPipelines.filter(input => !pipelinesToDelete.includes(input));
        await this.context.context.globalState.update('userPipelines', updatedPipelines);
        vscode.window.showInformationMessage(`Deleted pipelines: '${pipelinesToDelete}.`);
      } else {
        vscode.window.showInformationMessage(`No pipelines deleted.`);
      }
      return;
    } else {
      pipeline = picked;
    }

    // If user canceled
    if (!pipeline)
      return;


    const replacmentVals = {
      filePath: currentFileUri.path,
      fileDir: path.dirname(currentFileUri.path),
      fileName: path.basename(currentFileUri.path),
      fileNameWithoutExtension: path.parse(currentFileUri.path).name
    }

    // Get passes from LSP server
    let result1: LlvmGetPassList.Response = undefined;
    try {
      const params: LlvmGetPassList.Params = {
        uri: currentFileUri.toString(),
        pipeline: pipeline,
        additional_opt_args: vscode.workspace.getConfiguration("llvm").get<string[]>("additionalOptArgs", []).map(str => format(str, replacmentVals))
      };

      const response = await client.sendRequest(LlvmGetPassList.Type, params);
      result1 = response;
    } catch (error) {
      vscode.window.showErrorMessage(`${error}`);
      this.context.outputChannel.appendLine(`Error during custom request LlvmGetPassList: ${error}`);
      return;
    }

    // If getting passes for this pipeline didn't fail, store it
    // Store it if not already present
    if (!predefinedPipelines.includes(pipeline) && !storedPipelines.includes(pipeline)) {
      const updated = [...storedPipelines, pipeline];
      await this.context.context.globalState.update('userPipelines', updated);
    }

    // Parse Result to extract out the PassList and Pass Description
    const passNames = result1['list'];
    const passDescriptions = result1['descriptions'];

    const patterns = filter_pases.map(p => new RegExp(p));
    const passItems: vscode.QuickPickItem[] = passNames.filter((name, idx) => patterns.some(regex => regex.test(name))).map((name, idx) => ({
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
    const passNumMatch = selectedPass.match(/^(\d+)/);
    const passNum = passNumMatch ? parseInt(passNumMatch[1]) : 0;
    this.context.outputChannel.appendLine(`You selected Pass: ${selectedPass} and PassNumber ${passNum}`);

    // Query server for filepath
    let result2: LlvmGetIRBeforePass.Response = undefined;
    try {
      const params: LlvmGetIRBeforePass.Params = {
        uri: currentFileUri.toString(),
        passnumber: passNum,
        pipeline: pipeline,
        pipeline_opt_args: vscode.workspace.getConfiguration("llvm").get<string[]>("additionalOptArgs", []).map(str => format(str, replacmentVals))
      };

      const response = await client.sendRequest(LlvmGetIRBeforePass.Type, params);
      result2 = response;
    } catch (error) {
      this.context.outputChannel.appendLine(`Error during custom request LlvmGetPassList: ${error}`);
      return;
    }
    this.context.outputChannel.appendLine(`Received IR File Path: ${result2['uri']}`);
    const cfgFilePath = vscode.Uri.parse(result2['uri']).fsPath;

    this.context.outputChannel.appendLine(`Trying to open: ${cfgFilePath}`);
    vscode.workspace.openTextDocument(cfgFilePath).then(doc => {
      vscode.window.showTextDocument(doc);
    });
  }
}
