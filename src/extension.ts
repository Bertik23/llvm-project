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
      const nodeToCenter = "node1"; // Or get this dynamically
      panel.webview.html = getWebviewContentWithInteraction(targetFileContent, targetFileName, nodeToCenter);
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

function getWebviewContentWithInteraction(svgContent: string, fileName: string, initialNodeToCenter?: string): string {
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
          background-color: #282c34;
          display: flex;
          align-items: center;
          justify-content: center;
          font-family: sans-serif;
      }
      #svg-container {
          width: 100%;
          height: 100%;
          cursor: default; /* Default cursor */
          overflow: hidden;
          user-select: none;
      }
      #svg-container.pannable-ctrl { /* When Ctrl is pressed (potential for Ctrl+Drag pan) */
          cursor: grab;
      }
      #svg-container.panning { /* When actively panning (Ctrl+Drag OR Middle Mouse Drag) */
          cursor: grabbing;
      }
      svg {
          transform-origin: 0 0;
          user-select: none;
      }

      svg *[id].node:hover:not(:has(text:hover):not(:has(tspan:hover))),
      svg *[id].edge:hover:not(:has(text:hover):not(:has(tspan:hover))) {
          opacity: 0.7;
      }

      svg *[id].node text,
      svg *[id].edge text,
      svg *[id].node tspan,
      svg *[id].edge tspan {
          cursor: text;
          user-select: text;
      }
  </style>
</head>
<body>
  <div id="svg-container">
      ${svgContent}
  </div>

  <script>
      const vscode = acquireVsCodeApi();

      const svgContainer = document.getElementById('svg-container');
      const svgElement = svgContainer.querySelector('svg');

      let scale = 1;
      let panOffset = { x: 0, y: 0 };
      let isPanning = false; // True if Ctrl+Drag OR Middle Mouse Drag is active
      let ctrlPressed = false;
      let startPoint = { x: 0, y: 0 };
      let dragThreshold = 3;
      let dragStartPos = {x: 0, y: 0};
      let hasDragged = false;
      let panInitiatorButton = -1; // 0 for left, 1 for middle

      window.addEventListener('keydown', (event) => {
          if (event.key === 'Control' && !ctrlPressed) {
              ctrlPressed = true;
              if (!isPanning) {
                  svgContainer.classList.add('pannable-ctrl');
              }
          }
      });
      window.addEventListener('keyup', (event) => {
          if (event.key === 'Control') {
              ctrlPressed = false;
              // If not actively panning, remove pannable-ctrl.
              // If panning was initiated by Ctrl+Drag and Ctrl is released, panning continues until mouseup.
              if (!isPanning) {
                  svgContainer.classList.remove('pannable-ctrl');
                  svgContainer.classList.remove('panning'); // Defensive
              }
          }
      });
      window.addEventListener('blur', () => {
          if (ctrlPressed) {
              ctrlPressed = false;
              svgContainer.classList.remove('pannable-ctrl');
          }
          if (isPanning) {
              isPanning = false;
              svgContainer.classList.remove('panning');
               // If Ctrl was pressed, restore pannable-ctrl, else default.
              if (ctrlPressed) svgContainer.classList.add('pannable-ctrl');
          }
           panInitiatorButton = -1;
      });

      if (svgElement) {
          svgElement.style.transformOrigin = '0 0';
          updateTransform();

          function centerOnNode(nodeId) {
              // ... (centering logic - no changes needed here)
              const nodeElement = svgElement.querySelector(\`#\${nodeId}\`);
              if (nodeElement && svgContainer) {
                  const nodeRect = nodeElement.getBoundingClientRect();
                  const containerRect = svgContainer.getBoundingClientRect();
                  const currentScale = scale;
                  const nodeCenterXInViewport = nodeRect.left + nodeRect.width / 2;
                  const nodeCenterYInViewport = nodeRect.top + nodeRect.height / 2;
                  const containerCenterXInViewport = containerRect.left + containerRect.width / 2;
                  const containerCenterYInViewport = containerRect.top + containerRect.height / 2;
                  const dxViewport = containerCenterXInViewport - nodeCenterXInViewport;
                  const dyViewport = containerCenterYInViewport - nodeCenterYInViewport;
                  panOffset.x += dxViewport / currentScale;
                  panOffset.y += dyViewport / currentScale;
                  updateTransform();
              }
          }

          const initialNodeId = "${initialNodeToCenter || ''}";
          if (initialNodeId) {
              setTimeout(() => centerOnNode(initialNodeId), 100);
          }

          svgContainer.addEventListener('wheel', (event) => {
              if (!ctrlPressed) return; // Zoom only with Ctrl + Wheel
              event.preventDefault();
              // ... (zoom logic - no changes needed here)
              const zoomIntensity = 0.1;
              const direction = event.deltaY < 0 ? 1 : -1;
              const oldScale = scale;
              scale += direction * zoomIntensity * scale;
              scale = Math.max(0.05, Math.min(scale, 20));
              const rect = svgContainer.getBoundingClientRect();
              const mouseX = event.clientX - rect.left;
              const mouseY = event.clientY - rect.top;
              panOffset.x = mouseX - (mouseX - panOffset.x) * (scale / oldScale);
              panOffset.y = mouseY - (mouseY - panOffset.y) * (scale / oldScale);
              updateTransform();
          }, { passive: false });

          svgContainer.addEventListener('mousedown', (event) => {
              // Check if the target is text that should be selectable (for left click without Ctrl)
              let target = event.target;
              let isTextTarget = false;
              while(target && target !== svgElement) {
                  if ((target.tagName === 'text' || target.tagName === 'tspan') &&
                      target.closest && target.closest('.node, .edge')) {
                      isTextTarget = true;
                      break;
                  }
                  target = target.parentElement;
              }

              if (isTextTarget && !ctrlPressed && event.button === 0) {
                  // Allow default mousedown for text selection if Ctrl is not pressed (left click)
                  return;
              }

              // Panning with Ctrl + Left Mouse (button 0)
              if (ctrlPressed && event.button === 0) {
                  isPanning = true;
                  panInitiatorButton = 0;
                  hasDragged = false;
                  svgContainer.classList.remove('pannable-ctrl');
                  svgContainer.classList.add('panning');
                  dragStartPos = { x: event.clientX, y: event.clientY };
                  startPoint = { x: event.clientX - panOffset.x, y: event.clientY - panOffset.y };
                  event.preventDefault();
              }
              // Panning with Middle Mouse (button 1)
              else if (event.button === 1) {
                  isPanning = true;
                  panInitiatorButton = 1;
                  hasDragged = false;
                  svgContainer.classList.remove('pannable-ctrl'); // Ensure pannable-ctrl is off if active
                  svgContainer.classList.add('panning');
                  dragStartPos = { x: event.clientX, y: event.clientY };
                  startPoint = { x: event.clientX - panOffset.x, y: event.clientY - panOffset.y };
                  event.preventDefault(); // Prevent default middle-click actions (e.g., autoscroll)
              }
              // For non-Ctrl, non-Middle-Mouse clicks (potential element selection)
              else if (event.button === 0) { // Left click without Ctrl
                  hasDragged = false;
                  dragStartPos = { x: event.clientX, y: event.clientY };
                  // Don't preventDefault, might be a click on a node/edge
              }
          });

          document.addEventListener('mousemove', (event) => {
              if (!isPanning) return; // Only if panning is active (Ctrl+Drag OR Middle Mouse Drag)

              if (!hasDragged) {
                  const dx = event.clientX - dragStartPos.x;
                  const dy = event.clientY - dragStartPos.y;
                  if (Math.abs(dx) > dragThreshold || Math.abs(dy) > dragThreshold) {
                      hasDragged = true;
                  }
              }

              if (hasDragged) {
                  // No need to check initiator button for move, just pan
                  event.preventDefault(); // Prevent text selection during pan
                  panOffset.x = event.clientX - startPoint.x;
                  panOffset.y = event.clientY - startPoint.y;
                  updateTransform();
              }
          });

          document.addEventListener('mouseup', (event) => {
              // Only act if panning was active AND the released button matches the one that started the pan
              // OR if any button up while isPanning (simpler, but less precise if other buttons are involved, though unlikely for pan)
              if (isPanning && (event.button === panInitiatorButton || panInitiatorButton === -1 /* safety */) ) {
                  isPanning = false;
                  panInitiatorButton = -1;
                  svgContainer.classList.remove('panning');
                  if (ctrlPressed) { // If Ctrl is still held (relevant if pan was Ctrl+Left)
                      svgContainer.classList.add('pannable-ctrl');
                  } else {
                      svgContainer.classList.remove('pannable-ctrl'); // Ensure it's off
                  }
              }

              // Click detection logic (if not a drag, not Ctrl+Click, not MiddleClick during its pan)
              // This needs to be careful not to fire if the mouseup was ending a middle-mouse pan.
              // 'hasDragged' will be true if middle mouse was dragged.
              // If middle mouse was clicked without drag, 'hasDragged' is false. We don't want to postMessage for middle click.
              if (!hasDragged && event.button === 0 && !ctrlPressed) { // Left click, no drag, no ctrl
                  let clickedElementTarget = event.target;
                  // ... (rest of the click detection logic for posting messages - no changes needed here)
                  let clickableElement = null;
                  while (clickedElementTarget && clickedElementTarget !== svgElement) {
                      if (clickedElementTarget.id && clickedElementTarget.classList &&
                          (clickedElementTarget.classList.contains('node') || clickedElementTarget.classList.contains('edge'))) {
                          clickableElement = clickedElementTarget;
                          break;
                      }
                      clickedElementTarget = clickedElementTarget.parentElement;
                  }
                  if (clickableElement) {
                      let actualTarget = event.target;
                      let isTextClick = false;
                      while(actualTarget && actualTarget !== clickableElement.parentElement) {
                          if ((actualTarget.tagName === 'text' || actualTarget.tagName === 'tspan') && actualTarget.closest('#'+clickableElement.id)) {
                              isTextClick = true;
                              break;
                          }
                          if (actualTarget === clickableElement) break;
                          actualTarget = actualTarget.parentElement;
                      }
                      if (!isTextClick) {
                           console.log('Clicked SVG element ID:', clickableElement.id);
                           vscode.postMessage({
                               command: 'svgElementClicked',
                               elementId: clickableElement.id
                           });
                      } else {
                          console.log('Clicked on text within node/edge, allowing selection.');
                      }
                  }
              }
              hasDragged = false; // Reset for next mousedown sequence
          });

          function updateTransform() {
              if (svgElement) {
                  svgElement.style.transform = \`translate(\${panOffset.x}px, \${panOffset.y}px) scale(\${scale})\`;
              }
          }
      } else {
          console.error("SVG element not found inside #svg-container");
      }
  </script>
</body>
</html>`;
}
