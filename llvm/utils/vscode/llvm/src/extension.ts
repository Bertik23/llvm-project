import * as vscode from 'vscode';

import { LITTaskProvider } from './litTaskProvider';
import { LLVMContext } from './llvmContext';
import { LLVMGetCfgCommand } from './llvmCfg';
import { LLVMGetIRCommand } from './llvmPipeline';

let litTaskProvider: vscode.Disposable | undefined;
let customTaskProvider: vscode.Disposable | undefined;

/**
 *  This method is called when the extension is activated. The extension is
 *  activated the very first time a command is executed.
 */
export function activate(context: vscode.ExtensionContext) {
	litTaskProvider = vscode.tasks.registerTaskProvider(LITTaskProvider.LITType, new LITTaskProvider());

  const outputChannel = vscode.window.createOutputChannel('llvm-lsp-server', 'llvm');
  context.subscriptions.push(outputChannel);

  const llvmContext = new LLVMContext(context, outputChannel);
  context.subscriptions.push(llvmContext);

  // Initialize the commands of the extension.
  context.subscriptions.push(
    vscode.commands.registerCommand('llvm.restart', async () => {
      // Dispose and reactivate the context.
      llvmContext.dispose();
      await llvmContext.activate();
    }));

  new LLVMGetCfgCommand(llvmContext);
  new LLVMGetIRCommand(llvmContext);

  llvmContext.activate();
  outputChannel.appendLine("LLVM: extension activated!");
}

export function deactivate(): void {
	if (litTaskProvider) {
		litTaskProvider.dispose();
	}
}
