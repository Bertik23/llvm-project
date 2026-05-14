import * as vscode from "vscode";
import { Command } from "./command";
import { LLVMContext } from "./llvmContext";
import * as fs from "fs";
import { exec } from "child_process";
import { promisify } from "util";
import * as path from "path";
import * as config from "./config";
import * as csv from "csv-parse/sync";

const execAsync = promisify(exec);

export class LLVMObjdumpCommand extends Command {
  constructor(context: LLVMContext) {
    console.log("Objdump registered");
    super("llvm.get_objdump", context);
  }

  async execute(uri: vscode.Uri, ...args: any[]) {
    // Determine new file path (e.g., main.o -> main.asm)
    const newFilePath = uri.fsPath + ".asm";
    const newFileUri = vscode.Uri.file(newFilePath);
    const fileName = path.basename(uri.fsPath);
    // Extract {name} from {name}.opt.o
    // This removes '.opt.o' from the end of the string
    const dir = path.dirname(uri.fsPath);
    const baseName = fileName.endsWith(".opt.o")
      ? fileName.slice(0, -6)
      : path.parse(fileName).name;

    try {
      const { stdout, stderr } = await execAsync(
        `${config.getInstallPath()}llvm-objdump -dr --no-show-raw-insn --fault-map-section '${uri.fsPath}' --x86-asm-syntax=intel`,
        {maxBuffer: 1024 * 1024 * 1024 /* 1GB */}
      );

      if (stderr) {
        vscode.window.showErrorMessage(`Objdump Error: ${stderr}`);
        return;
      }

      // Write the file to disk
      fs.writeFileSync(newFilePath, stdout);

      // Open the newly created file in the editor
      const document = await vscode.workspace.openTextDocument(newFileUri);
      await vscode.window.showTextDocument(document);

      vscode.window.showInformationMessage(
        `Disassembly saved to ${newFilePath}`,
      );
    } catch (error) {
      vscode.window.showErrorMessage(`Failed to disassemble: ${error.message}`);
    }
  }
}
