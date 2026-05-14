import * as vscode from 'vscode';
import * as fs from 'fs';
import * as path from 'path';
import * as csv from 'csv-parse/sync';
import { Command } from './command';
import { LLVMContext } from './llvmContext';

export class LLVMFilePicker extends Command {

  constructor(context: LLVMContext) {
    super('llvm.pick_file', context);
  }

  async execute(...args: any[]) {
        // Use workspace folder or let user pick directory
        const workspaceFolders = vscode.workspace.workspaceFolders;
        if (!workspaceFolders) {
            vscode.window.showErrorMessage('No workspace folder open.');
            return;
        }

        const workspaceDir = workspaceFolders[0].uri.fsPath;
        const filePath = await showLLFilePicker(workspaceDir);

        if (filePath) {
            const doc = await vscode.workspace.openTextDocument(filePath);
            await vscode.window.showTextDocument(doc);
        }
    }
}

interface ProfileEntry {
    percent: number;
    samples: number;
    shared_object: string;
    compile_id: string;
    symbol: string;
    filename: string | null;
}

interface LLFileItem extends vscode.QuickPickItem {
    filePath: string;
    percent: number | null;
}

function parseProfileCSV(csvPath: string): Map<string, ProfileEntry> {
    const fileMap = new Map<string, ProfileEntry>();

    if (!fs.existsSync(csvPath)) {
        return fileMap;
    }

    const content = fs.readFileSync(csvPath, 'utf-8');
    const records = csv.parse(content, {
        columns: true,
        skip_empty_lines: true,
        relax_quotes: true,
        trim: true,
    });

    for (const record of records) {
        const symbol: string = record['symbol'] || '';
        const percentStr: string = record['percent'] || '0';
        const percent = parseFloat(percentStr.replace('%', ''));

        // Extract .ll filename from symbol if present
        const llMatch = symbol.match(/([^\s/]+\.ll)$/);
        if (llMatch) {
            const filename = llMatch[1];
            fileMap.set(filename, {
                percent,
                samples: parseInt(record['samples'] || '0'),
                shared_object: record['shared_object'] || '',
                compile_id: record['compile_id'] || '',
                symbol,
                filename,
            });
        }
    }

    return fileMap;
}

function getHotnessIcon(percent: number): string {
    if (percent >= 20) return '🔴';
    if (percent >= 10) return '🟠';
    if (percent >= 5)  return '🟡';
    if (percent >= 1)  return '🟢';
    return '🔵';
}

function formatPercent(percent: number): string {
    return percent.toFixed(2).padStart(5) + '%';
}

export async function showLLFilePicker(workspaceDir: string): Promise<string | undefined> {
    const csvPath = path.join(workspaceDir, '0_profile_summary.csv');
    const profileMap = parseProfileCSV(csvPath);

    // Get all .ll files in directory
    const allFiles = fs.readdirSync(workspaceDir).filter(f => f.endsWith('.ll'));

    if (allFiles.length === 0) {
        vscode.window.showWarningMessage('No .ll files found in the directory.');
        return undefined;
    }

    // Build quick pick items
    const maxPercent = Math.max(
        ...Array.from(profileMap.values()).map(e => e.percent),
        1
    );

    const items: LLFileItem[] = allFiles
        .map((filename): LLFileItem => {
            const entry = profileMap.get(filename);
            if (entry) {
                const icon = getHotnessIcon(entry.percent);
                const pct = formatPercent(entry.percent);
                return {
                    label: filename,
                    description: `${entry.samples.toLocaleString()} samples`,
                    detail: `${icon} ${pct} ${entry.symbol}`,
                    filePath: path.join(workspaceDir, filename),
                    percent: entry.percent,
                    alwaysShow: true,
                };
            } else {
                return {
                    label: filename,
                    description: 'Not in profile',
                    detail: 'No profiling data available for this file',
                    filePath: path.join(workspaceDir, filename),
                    percent: null,
                    alwaysShow: true,
                };
            }
        })
        // Sort: profiled files first by percent desc, then unprofiled alphabetically
        .sort((a, b) => {
            if (a.percent !== null && b.percent !== null) return b.percent - a.percent;
            if (a.percent !== null) return -1;
            if (b.percent !== null) return 1;
            return a.label.localeCompare(b.label);
        });

    const picked = await vscode.window.showQuickPick(items, {
        title: '🔥 Select .ll File (sorted by hotness)',
        placeHolder: 'Search for a .ll file...',
        matchOnDescription: true,
        matchOnDetail: true,
    });

    return picked?.filePath;
}
