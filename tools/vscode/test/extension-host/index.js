/* eslint-disable no-await-in-loop */
"use strict";

const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const vscode = require("vscode");
const manifest = require("../../package.json");

const extensionId = `${manifest.publisher}.${manifest.name}`;
const tracePath = path.join(os.tmpdir(), "frothy-editor-smoke-trace.log");

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function withTimeout(promise, timeoutMs, label) {
  let timer = null;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => {
      reject(new Error(`timed out waiting for ${label}`));
    }, timeoutMs);
  });
  try {
    return await Promise.race([promise, timeout]);
  } finally {
    if (timer !== null) {
      clearTimeout(timer);
    }
  }
}

function trace(step) {
  fs.appendFileSync(tracePath, `${step}\n`, "utf8");
}

async function waitForOutput(api, pattern, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (api.getOutputText().includes(pattern)) {
      return;
    }
    await sleep(50);
  }
  throw new Error(`timed out waiting for output: ${pattern}`);
}

async function openEditor(fileName) {
  const folder = vscode.workspace.workspaceFolders?.[0];
  if (!folder) {
    throw new Error("workspace folder not available");
  }
  const filePath = path.join(folder.uri.fsPath, fileName);
  const document = await vscode.workspace.openTextDocument(filePath);
  return vscode.window.showTextDocument(document, { preview: false });
}

async function restoreEditorSmokeFixtures() {
  const folder = vscode.workspace.workspaceFolders?.[0];
  if (!folder) {
    return;
  }
  const fixtures = new Map([
    ["send-file.frothy", "keep = 30\nprobe(n) { n + 2 }\n"],
  ]);
  for (const [fileName, contents] of fixtures) {
    const uri = vscode.Uri.file(path.join(folder.uri.fsPath, fileName));
    await vscode.workspace.fs.writeFile(uri, Buffer.from(contents, "utf8"));
  }
}

async function replaceDocument(editor, text) {
  const fullRange = new vscode.Range(
    editor.document.positionAt(0),
    editor.document.positionAt(editor.document.getText().length),
  );
  const ok = await editor.edit((editBuilder) => {
    editBuilder.replace(fullRange, text);
  });
  assert.ok(ok, "editor update should succeed");
}

function setLineCursor(editor, line) {
  const position = new vscode.Position(line, 0);
  editor.selection = new vscode.Selection(position, position);
}

async function configureExtension() {
  const cliPath = process.env.FROTHY_VSCODE_SMOKE_CLI_PATH;
  const port = process.env.FROTHY_VSCODE_SMOKE_PORT ?? "";
  if (!cliPath) {
    throw new Error("FROTHY_VSCODE_SMOKE_CLI_PATH is required");
  }

  await vscode.workspace
    .getConfiguration("frothy")
    .update("cliPath", cliPath, vscode.ConfigurationTarget.Workspace);
  await vscode.workspace
    .getConfiguration("frothy")
    .update("port", port, vscode.ConfigurationTarget.Workspace);
}

async function expectSee(api, name, pattern) {
  api.enqueueInputBoxResponse(name);
  await vscode.commands.executeCommand("frothy.see");
  await waitForOutput(api, pattern);
}

async function expectNamedCommand(api, command, name, pattern) {
  api.enqueueInputBoxResponse(name);
  await vscode.commands.executeCommand(command);
  await waitForOutput(api, pattern);
}

async function run() {
  fs.writeFileSync(tracePath, "", "utf8");
  try {
    trace("start");
    const extension = vscode.extensions.getExtension(extensionId);
    if (!extension) {
      throw new Error(`${extensionId} extension not found`);
    }

    const api = await extension.activate();
    trace("activated");
    await configureExtension();
    trace("configured");
    if (process.env.FROTHY_VSCODE_SMOKE_REAL_CLI) {
      process.env.FROTHY_TEST_REAL_CLI = process.env.FROTHY_VSCODE_SMOKE_REAL_CLI;
    }
    if (process.env.FROTHY_VSCODE_SMOKE_LOCAL_RUNTIME) {
      process.env.FROTHY_TEST_LOCAL_RUNTIME =
        process.env.FROTHY_VSCODE_SMOKE_LOCAL_RUNTIME;
    }

    const lineEditor = await openEditor("line-send.frothy");
    await replaceDocument(
      lineEditor,
      "control.value = 41 + 1\nwhile true { keep }\nafter_interrupt = 7\n",
    );
    setLineCursor(lineEditor, 0);
    trace("line editor ready");

    await vscode.commands.executeCommand("frothy.connect");
    await api.waitForState("connected", 15000);
    assert.ok(api.getSnapshot().device, "device should be connected");
    trace("connected");

    api.clearOutput();
    await vscode.commands.executeCommand("frothy.sendSelection");
    await expectSee(api, "control.value", "42");
    trace("selection ok");

    let fileEditor = await openEditor("send-file.frothy");
    await replaceDocument(
      fileEditor,
      "keep = 10\ndrop = 20\nprobe(n) { n + 1 }\n",
    );
    api.clearOutput();
    await vscode.commands.executeCommand("frothy.sendFile");
    await waitForOutput(api, "[frothy] reset");
    await expectSee(api, "drop", "20");
    trace("first send file ok");

    await replaceDocument(
      fileEditor,
      "keep = 30\nprobe(n) { n + 2 }\n",
    );
    api.clearOutput();
    await vscode.commands.executeCommand("frothy.sendFile");
    await waitForOutput(api, "[frothy] reset");
    await expectSee(api, "drop", "see failed");
    await expectSee(api, "keep", "30");
    trace("second send file ok");

    api.clearOutput();
    await vscode.commands.executeCommand("frothy.words");
    await waitForOutput(api, "probe");
    assert.ok(!/\bdrop\b/.test(api.getOutputText()), "drop should be gone after reset + eval");
    trace("words ok");

    api.clearOutput();
    await expectNamedCommand(api, "frothy.core", "probe", "[frothy] core probe");
    trace("core ok");

    api.clearOutput();
    await expectNamedCommand(
      api,
      "frothy.slotInfo",
      "probe",
      "[frothy] slotInfo probe",
    );
    trace("slotInfo ok");

    api.clearOutput();
    await vscode.commands.executeCommand("frothy.save");
    await waitForOutput(api, "[frothy] save");
    trace("save ok");

    let runtimeEditor = await openEditor("line-send.frothy");
    setLineCursor(runtimeEditor, 0);
    await replaceDocument(
      runtimeEditor,
      "keep = 99\nwhile true { keep }\nafter_interrupt = 7\n",
    );
    setLineCursor(runtimeEditor, 0);
    api.clearOutput();
    await vscode.commands.executeCommand("frothy.sendSelection");
    await expectSee(api, "keep", "99");
    trace("mutate keep ok");

    api.clearOutput();
    await vscode.commands.executeCommand("frothy.restore");
    await waitForOutput(api, "[frothy] restore");
    await expectSee(api, "keep", "30");
    trace("restore ok");

    setLineCursor(runtimeEditor, 1);
    api.clearOutput();
    const runningEval = vscode.commands.executeCommand("frothy.sendSelection");
    await api.waitForState("running", 15000);
    await vscode.commands.executeCommand("frothy.interrupt");
    await withTimeout(runningEval, 15000, "interrupted eval to settle");
    await api.waitForState("connected", 15000);
    trace("interrupt ok");

    fileEditor = await openEditor("send-file.frothy");
    await replaceDocument(fileEditor, "file_after_interrupt = 44\n");
    runtimeEditor = await openEditor("line-send.frothy");
    setLineCursor(runtimeEditor, 1);
    api.clearOutput();
    const supersededEval = vscode.commands.executeCommand("frothy.sendSelection");
    await api.waitForState("running", 15000);
    await vscode.window.showTextDocument(fileEditor.document, { preview: false });
    await vscode.commands.executeCommand("frothy.sendFile");
    await withTimeout(supersededEval, 15000, "superseded eval to settle");
    await api.waitForState("connected", 15000);
    await waitForOutput(api, "send requested while running");
    await waitForOutput(api, "[frothy] reset");
    api.clearOutput();
    await expectSee(api, "file_after_interrupt", "44");
    api.clearOutput();
    await expectSee(api, "probe", "see failed");
    trace("send file while running ok");

    runtimeEditor = await openEditor("line-send.frothy");
    setLineCursor(runtimeEditor, 2);
    api.clearOutput();
    await vscode.commands.executeCommand("frothy.sendSelection");
    await expectSee(api, "after_interrupt", "7");
    trace("post interrupt ok");

    api.clearOutput();
    api.enqueueWarningResponse("Wipe Snapshot");
    await vscode.commands.executeCommand("frothy.wipe");
    await waitForOutput(api, "[frothy] wipe");
    await expectSee(api, "keep", "see failed");
    trace("wipe ok");

    runtimeEditor = await openEditor("line-send.frothy");
    await replaceDocument(runtimeEditor, "while true { nil }\n");
    setLineCursor(runtimeEditor, 0);
    api.clearOutput();
    const disconnectEval = vscode.commands.executeCommand("frothy.sendSelection");
    await api.waitForState("running", 15000);
    await vscode.commands.executeCommand("frothy.disconnect");
    await withTimeout(disconnectEval, 15000, "disconnect eval to settle");
    await api.waitForState("idle", 15000);
    trace("disconnect while running ok");

    api.clearOutput();
    await vscode.commands.executeCommand("frothy.connect");
    await api.waitForState("connected", 15000);
    await vscode.commands.executeCommand("frothy.disconnect");
    await api.waitForState("idle", 15000);
    trace("reconnect ok");

    await restoreEditorSmokeFixtures();
    trace("fixtures restored");

    const mode = process.env.FROTHY_VSCODE_SMOKE_MODE ?? "local";
    const report = [
      `mode=${mode}`,
      `device=${api.getSnapshot().device ? "connected" : "disconnected"}`,
    ].join("\n");
    const reportPath = path.join(os.tmpdir(), "frothy-editor-smoke-report.txt");
    fs.writeFileSync(reportPath, `${report}\n`, "utf8");
    trace("done");
    await vscode.commands.executeCommand("workbench.action.closeWindow");
  } catch (err) {
    try {
      const extension = vscode.extensions.getExtension(extensionId);
      const api = extension && extension.isActive ? extension.exports : null;
      if (api && typeof api.getOutputText === "function") {
        trace(`output:\n${api.getOutputText()}`);
      }
    } catch {
      // Best effort.
    }
    trace(`error: ${err.stack || err}`);
    try {
      await restoreEditorSmokeFixtures();
      trace("fixtures restored after failure");
    } catch (restoreErr) {
      trace(`fixture restore failed: ${restoreErr.stack || restoreErr}`);
    }
    throw err;
  }
}

module.exports = { run };
