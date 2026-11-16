'use strict';

// Minimal file-mailbox consumer.
// Watches <workspaceRoot>/prompt.txt, reads it, deletes it, then injects the
// text into Cursor's Chat input and submits it.

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

const PROMPT_FILE = 'prompt.txt';
const ERROR_LOG = 'prompt_error.log';
const POLL_MS = 500;

let watcher = null;
let pollTimer = null;
let busy = false;

function rootPath() {
  const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  if (!folder) return null;
  return folder.uri.fsPath;
}

function logError(root, err) {
  if (!root) return;
  const line = `[${new Date().toISOString()}] ${err && err.stack ? err.stack : String(err)}\n`;
  try {
    fs.appendFileSync(path.join(root, ERROR_LOG), line, 'utf8');
  } catch (_) {
    // Logging must never take the watcher down.
  }
}

async function injectIntoChat(text) {
  // Cursor's chat lives in a webview / editor input. The supported route is:
  // focus chat, set the editor text, then submit. Try the common commands in
  // order and fall back to clipboard paste.
  const tryCmds = [
    'aipopup.action.modal.generate',
    'workbench.action.chat.open',
    'cursor.chat.open'
  ];
  for (const cmd of tryCmds) {
    try {
      await vscode.commands.executeCommand(cmd);
      break;
    } catch (_) {
      // command not present in this Cursor build; try the next one
    }
  }

  // Prefer typing through the editor surface when a chat input editor exists.
  const injected = await vscode.commands.executeCommand('workbench.action.chat.open', {
    query: text,
    isPartialQuery: false
  }).then(
    () => true,
    () => false
  );
  if (injected) return;

  // Fallback: clipboard + paste, so the user still only needs to press Enter.
  await vscode.env.clipboard.writeText(text);
  await vscode.commands.executeCommand('editor.action.clipboardPasteAction');
}

async function consume(root) {
  const file = path.join(root, PROMPT_FILE);
  if (busy || !fs.existsSync(file)) return;
  busy = true;
  try {
    const text = fs.readFileSync(file, 'utf8');
    fs.unlinkSync(file); // delete before injecting to prevent double triggers
    await injectIntoChat(text);
  } catch (err) {
    logError(root, err);
  } finally {
    busy = false;
  }
}

function schedule(root) {
  clearInterval(pollTimer);
  pollTimer = setInterval(() => consume(root), POLL_MS);
}

function start() {
  const root = rootPath();
  if (!root) {
    vscode.window.showWarningMessage('L Prompt Mailbox: no workspace folder open.');
    return;
  }
  stop();
  // fs.watch is instant but unreliable on some filesystems/editors; polling is
  // the safety net. consume() is idempotent thanks to the unlink + busy guard.
  try {
    watcher = fs.watch(root, { persistent: false }, (_event, name) => {
      if (name === PROMPT_FILE) consume(root);
    });
    watcher.on('error', (err) => logError(root, err));
  } catch (err) {
    logError(root, err);
  }
  schedule(root);
  vscode.window.showInformationMessage('L Prompt Mailbox: watching prompt.txt');
}

function stop() {
  if (watcher) {
    try { watcher.close(); } catch (_) {}
    watcher = null;
  }
  clearInterval(pollTimer);
  pollTimer = null;
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand('lPromptMailbox.start', start),
    vscode.commands.registerCommand('lPromptMailbox.stop', () => {
      stop();
      vscode.window.showInformationMessage('L Prompt Mailbox: stopped');
    })
  );
  start();
}

function deactivate() {
  stop();
}

module.exports = { activate, deactivate };
