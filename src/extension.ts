import * as vscode from 'vscode';

import { LLVMContext } from './llvmContext';
import { LLVMCfgCommand } from './llvmCfgCommand';

/**
 *  This method is called when the extension is activated. The extension is
 *  activated the very first time a command is executed.
 */
export function activate(context: vscode.ExtensionContext) {
  const outputChannel = vscode.window.createOutputChannel('llvm-lsp-server', 'llvm');
  context.subscriptions.push(outputChannel);

  const llvmContext = new LLVMContext(context);
  context.subscriptions.push(llvmContext);

  // Initialize the commands of the extension.
  context.subscriptions.push(
    vscode.commands.registerCommand('llvm.restart', async () => {
      // Dispose and reactivate the context.
      llvmContext.dispose();
      await llvmContext.activate(outputChannel);
    }));

  new LLVMCfgCommand(llvmContext, outputChannel);

  llvmContext.activate(outputChannel);
  outputChannel.appendLine("LLVM: extension activated!");
}
