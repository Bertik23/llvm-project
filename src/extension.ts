import * as vscode from 'vscode';
import * as path from 'path';

import { LLVMContext } from './llvmContext';

/**
 *  This method is called when the extension is activated. The extension is
 *  activated the very first time a command is executed.
 */
export function activate(context: vscode.ExtensionContext) {
  const outputChannel = vscode.window.createOutputChannel('llvm-lsp-server', 'llvm');
  context.subscriptions.push(outputChannel);

  const llvmContext = new LLVMContext();
  context.subscriptions.push(llvmContext);

  // Initialize the commands of the extension.
  context.subscriptions.push(
    vscode.commands.registerCommand('llvm.restart', async () => {
      // Dispose and reactivate the context.
      llvmContext.dispose();
      await llvmContext.activate(outputChannel);
    }));

  context.subscriptions.push(
    vscode.commands.registerCommand('llvm.webview', async () => {
      const activeEditor = vscode.window.activeTextEditor;
      if (!activeEditor) {
        vscode.window.showInformationMessage('No active text editor.');
        return;
      }
      const currentFilePath = activeEditor.document.uri.fsPath;
      const currentDir = path.dirname(currentFilePath);
      const currentFileExtension = path.extname(currentFilePath);
      const currentFileNameWithoutExt = path.basename(currentFilePath, currentFileExtension);
      const targetExtension = '.svg';
      const targetFileName = currentFileNameWithoutExt + targetExtension;
      const targetFilePath = path.join(currentDir, targetFileName);
      let targetFileContent: string;
      try {
        const targetUri = vscode.Uri.file(targetFilePath);
        const fileBytes = await vscode.workspace.fs.readFile(targetUri);
        targetFileContent = Buffer.from(fileBytes).toString('utf8');
        // vscode.window.showInformationMessage(`Successfully read ${targetFileName}`);
      } catch (error) {
        vscode.window.showErrorMessage(`Could not read file: ${targetFilePath}. Error: ${error}`);
        console.error(`Error reading ${targetFilePath}:`, error);
        return;
      }

      const panel = vscode.window.createWebviewPanel(
        'embeddedCFGView',
        `CFG for ${targetFileName}`,
        vscode.ViewColumn.Beside,
        {
          enableScripts: true,
          localResourceRoots: [vscode.Uri.file(currentDir)]
        }
      );
      panel.webview.html = getWebviewContentWithInteraction(targetFileContent, targetFileName);
      // Handle messages from the webview
      context.subscriptions.push(
        panel.webview.onDidReceiveMessage(
          message => {
            switch (message.command) {
              case 'svgElementClicked':
                const elementId = message.elementId;
                vscode.window.showInformationMessage(`SVG Element Clicked: ID = ${elementId}`);
                // Here you can do something with the elementId,
                // like find it in a data structure, highlight something in another editor, etc.
                return;
            }
          },
          undefined,
          context.subscriptions
        ));
    })
  );

  llvmContext.activate(outputChannel);
  outputChannel.appendLine("LLVM: extension activated!");
}


function getWebviewContentWithInteraction(svgContent: string, fileName: string): string {
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Interactive ${fileName}</title>
  <style>
      body, html {
          margin: 0;
          padding: 0;
          width: 100%;
          height: 100%;
          overflow: hidden;
          background-color: #181818;
          display: flex;
          align-items: center;
          justify-content: center;
      }
      #svg-container {
          width: 100%;
          height: 100%;
          cursor: grab;
          overflow: hidden;
      }
      #svg-container.grabbing {
          cursor: grabbing;
      }
      svg {
          transform-origin: 0 0;
      }

      /* Apply pointer cursor only to elements with an ID AND class 'node' or 'edge' */
      svg *[id].node,
      svg *[id].edge {
          cursor: pointer;
      }

      /* Optional: highlight on hover for these specific elements */
      svg *[id].node:hover,
      svg *[id].edge:hover {
          opacity: 0.8;
      }
  </style>
</head>
<body>
  <div id="svg-container">
      ${svgContent}
  </div>

  <script>
      // Acquire the vscode API an
      const vscode = acquireVsCodeApi(); // IMPORTANT!

      const svgContainer = document.getElementById('svg-container');
      const svgElement = svgContainer.querySelector('svg');

      if (svgElement) {
          let scale = 1;
          let isPanning = false;
          let startPoint = { x: 0, y: 0 };
          let panOffset = { x: 0, y: 0 };
          let dragThreshold = 5; // Pixels to move before it's considered a drag, not a click
          let dragStartPos = {x: 0, y: 0};
          let hasDragged = false;


          svgElement.style.transformOrigin = '0 0';
          updateTransform();

          svgContainer.addEventListener('wheel', (event) => {
              event.preventDefault();
              const zoomIntensity = 0.1;
              const direction = event.deltaY < 0 ? 1 : -1;
              const oldScale = scale;
              scale += direction * zoomIntensity * scale;
              scale = Math.max(0.1, Math.min(scale, 10));

              const rect = svgContainer.getBoundingClientRect();
              const mouseX = event.clientX - rect.left;
              const mouseY = event.clientY - rect.top;

              panOffset.x = mouseX - (mouseX - panOffset.x) * (scale / oldScale);
              panOffset.y = mouseY - (mouseY - panOffset.y) * (scale / oldScale);

              updateTransform();
          });

          svgContainer.addEventListener('mousedown', (event) => {
              // Only pan with left mouse button
              if (event.button !== 0) return;

              isPanning = true;
              hasDragged = false; // Reset drag flag
              svgContainer.classList.add('grabbing');
              // Store initial mouse position for drag threshold detection
              dragStartPos = { x: event.clientX, y: event.clientY };
              // Pan start point relative to current pan
              startPoint = { x: event.clientX - panOffset.x, y: event.clientY - panOffset.y };
              // Do not preventDefault here immediately if we want click events to propagate
          });

          svgContainer.addEventListener('mousemove', (event) => {
              if (!isPanning) return;

              // Check for drag threshold
              const dx = event.clientX - dragStartPos.x;
              const dy = event.clientY - dragStartPos.y;
              if (!hasDragged && (Math.abs(dx) > dragThreshold || Math.abs(dy) > dragThreshold)) {
                  hasDragged = true; // It's a drag, not a click
              }

              if (hasDragged) { // Only pan if it's a confirmed drag
                  event.preventDefault(); // Prevent text selection, etc. during drag
                  panOffset.x = event.clientX - startPoint.x;
                  panOffset.y = event.clientY - startPoint.y;
                  updateTransform();
              }
          });

          const stopPanning = (event) => {
              if (isPanning) {
                  isPanning = false;
                  svgContainer.classList.remove('grabbing');
              }
          };
          svgContainer.addEventListener('mouseup', stopPanning);
          svgContainer.addEventListener('mouseleave', stopPanning);

          // CLICK EVENT LISTENER
          svgContainer.addEventListener('click', (event) => {
                // 1. First filter: Ignore if it was a drag, not a click
                if (hasDragged) {
                    hasDragged = false;
                    return; // Action (postMessage) is not performed
                }

                let targetElement = event.target; // The actual innermost element clicked
                let clickableElement = null;     // This will store our desired element if found

                // 2. Second filter: Traverse and identify the correct element
                //    The loop looks for an element that:
                //    a) Is the clicked element or one of its parents (up to the SVG root)
                //    b) Has an 'id' attribute
                //    c) Has the class 'node' OR 'edge'
                while (targetElement && targetElement !== svgElement) {
                    if (targetElement.id && // Must have an ID
                        targetElement.classList && // Must have a classList
                        (targetElement.classList.contains('node') || targetElement.classList.contains('edge'))) { // Must have 'node' or 'edge' class
                        clickableElement = targetElement; // Found an element matching ALL criteria
                        break; // Stop searching, we found our specific target
                    }
                    targetElement = targetElement.parentElement; // Check the parent
                }

                // 3. Third filter: Perform the action (postMessage) ONLY if a valid 'clickableElement' was found
                if (clickableElement) {
                    // This block is ONLY executed if 'clickableElement' is not null.
                    // 'clickableElement' is not null ONLY IF the 'while' loop above found an element
                    // that has an ID AND (class 'node' OR class 'edge').

                    console.log('Action: Sending message for ID:', clickableElement.id, 'Classes:', clickableElement.className);
                    vscode.postMessage({
                        command: 'svgElementClicked',
                        elementId: clickableElement.id
                    });
                } else if (targetElement && targetElement === svgElement) {
                    // This means the click was on the SVG background,
                    // or on an element that didn't meet the criteria in the 'while' loop.
                    console.log('Clicked on SVG background or non-target element. No action taken.');
                } else {
                    // Clicked on an SVG element that doesn't have an ID, or doesn't have 'node'/'edge' class.
                    console.log('Clicked on a non-target SVG element. No action taken.');
                }
          });


          function updateTransform() {
              svgElement.style.transform = \`translate(\${panOffset.x}px, \${panOffset.y}px) scale(\${scale})\`;
          }
      } else {
          console.error("SVG element not found inside #svg-container");
      }
  </script>
</body>
</html>`;
}
