#!/usr/bin/env node
"use strict";

const {
  ControlSessionClientError,
} = require("../out/control-session-client");
const { FrothyController } = require("../out/controller");

let passed = 0;
let failed = 0;
const failures = [];

function assert(cond, msg) {
  if (!cond) {
    throw new Error(msg);
  }
}

function assertEq(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

async function test(name, fn) {
  try {
    await fn();
    passed++;
    process.stdout.write(`  ok  ${name}\n`);
  } catch (err) {
    failed++;
    failures.push({ name, err });
    process.stdout.write(`  FAIL ${name}: ${err.message}\n`);
  }
}

class FakeOutput {
  constructor() {
    this.buffer = "";
  }

  show() {}

  append(text) {
    this.buffer += text;
  }

  appendLine(text) {
    this.buffer += `${text}\n`;
  }
}

class FakeDocument {
  constructor(fsPath) {
    this.uriScheme = "file";
    this.fsPath = fsPath;
    this.isDirty = false;
  }

  async save() {
    this.isDirty = false;
    return true;
  }
}

class FakeEditor {
  constructor(fsPath, lineText) {
    this.document = new FakeDocument(fsPath);
    this.lineText = lineText;
    this.selectionValue = "";
    this.name = "probe";
  }

  selectionText() {
    return this.selectionValue;
  }

  currentLineText() {
    return this.lineText;
  }

  selectedName() {
    return this.name;
  }
}

class FakeHost {
  constructor() {
    this.output = new FakeOutput();
    this.editor = new FakeEditor("/tmp/demo.frothy", "control.value = 41 + 1");
    this.warningResponses = [];
    this.inputResponses = [];
    this.warningMessages = [];
    this.errorMessages = [];
    this.terminals = [];
    this.pickedDevice = undefined;
    this.storedPort = "";
  }

  getActiveEditor() {
    return this.editor;
  }

  async showWarningMessage(message, ...items) {
    this.warningMessages.push({ message, items });
    return this.warningResponses.shift();
  }

  showErrorMessage(message) {
    this.errorMessages.push(message);
  }

  async showInputBox() {
    return this.inputResponses.shift();
  }

  async pickDevice(candidates) {
    this.pickCandidates = candidates;
    return this.pickedDevice;
  }

  createTerminal(options) {
    this.terminals.push(options);
    return { show() {} };
  }

  getConfiguredPort() {
    return "";
  }

  getStoredPort() {
    return this.storedPort;
  }

  setStoredPort(_key, value) {
    this.storedPort = value;
  }

  getWorkspaceCwd() {
    return process.cwd();
  }

  getWorkspaceCwdForPath() {
    return process.cwd();
  }
}

class FakeClient {
  constructor() {
    this.isRunning = true;
    this.eventListeners = [];
    this.exitListeners = [];
    this.connectImpl = async () => ({
      port: "/dev/cu.mock",
      board: "mock-board",
      version: "0.1.0-test",
      cell_bits: 32,
    });
    this.resetImpl = async () => ({});
    this.evalImpl = async () => ({ text: "nil" });
  }

  async start() {}

  onEvent(listener) {
    this.eventListeners.push(listener);
  }

  onExit(listener) {
    this.exitListeners.push(listener);
  }

  emit(event) {
    for (const listener of this.eventListeners) {
      listener(event);
    }
  }

  exit(error) {
    for (const listener of this.exitListeners) {
      listener(error);
    }
  }

  async connect(port) {
    this.connectCalls = (this.connectCalls || []).concat([port || ""]);
    const device = await this.connectImpl(port);
    if (device) {
      this.emit({ type: "event", event: "connected", device });
    }
    return device;
  }

  async disconnect() {
    this.emit({ type: "event", event: "disconnected" });
  }

  async eval(source) {
    this.evalCalls = (this.evalCalls || []).concat([source]);
    return this.evalImpl(source);
  }

  async reset() {
    return this.resetImpl();
  }

  async interrupt() {}

  async words() {
    return { words: ["save", "restore"] };
  }

  async see(name) {
    return {
      name,
      is_overlay: true,
      value_class: 5,
      rendered: "42",
    };
  }

  async save() {
    return { text: "nil" };
  }

  async restore() {
    return { text: "nil" };
  }

  async wipe() {
    return { text: "nil" };
  }

  async core() {
    return { text: "(core)" };
  }

  async slotInfo() {
    return { text: "(slot)" };
  }

  dispose() {
    this.disposed = true;
  }
}

function createController(host, client, overrides = {}) {
  return new FrothyController({
    host,
    async resolveCliPath() {
      return "/tmp/froth";
    },
    createClient() {
      return client;
    },
    async resolveSendSource() {
      return { source: overrides.sendSource || "keep = 1" };
    },
  });
}

async function main() {
  process.stdout.write("\n=== Frothy controller smoke tests ===\n\n");

  await test("connect failure offers doctor on no_devices", async () => {
    const host = new FakeHost();
    host.warningResponses.push("Run Doctor");
    const client = new FakeClient();
    client.connectImpl = async () => {
      throw new ControlSessionClientError({
        code: "no_devices",
        message: "no Frothy device found",
      });
    };

    const controller = createController(host, client);
    await controller.connectToDevice();
    assertEq(host.terminals.length, 1, "doctor terminal should open");
    assertEq(controller.getSnapshot().state, "disconnected", "connect state");
  });

  await test("multiple device connect retries the picked port", async () => {
    const host = new FakeHost();
    host.pickedDevice = { port: "/dev/cu.picked", board: "picked", version: "0.1" };
    const client = new FakeClient();
    client.connectImpl = async (port) => {
      if (!port) {
        throw new ControlSessionClientError({
          code: "multiple_devices",
          message: "multiple Frothy devices found",
          candidates: [
            { port: "/dev/cu.picked", board: "picked", version: "0.1.0-test" },
          ],
        });
      }
      return {
        port,
        board: "picked",
        version: "0.1.0-test",
        cell_bits: 32,
      };
    };

    const controller = createController(host, client);
    const ok = await controller.connectToDevice();
    assertEq(ok, true, "connect should succeed");
    assertEq(client.connectCalls.length, 2, "connect should retry");
    assertEq(client.connectCalls[1], "/dev/cu.picked", "picked port");
    assertEq(controller.getSnapshot().state, "connected", "controller state");
  });

  await test("failed reconnect clears stale device metadata", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    const controller = createController(host, client);

    await controller.connectToDevice();
    assert(controller.getSnapshot().device, "device should be present after connect");

    host.pickedDevice = undefined;
    client.connectImpl = async () => {
      throw new ControlSessionClientError({
        code: "multiple_devices",
        message: "multiple Frothy devices found",
        candidates: [
          { port: "/dev/cu.a", board: "A", version: "0.1.0-test" },
          { port: "/dev/cu.b", board: "B", version: "0.1.0-test" },
        ],
      });
    };

    const ok = await controller.connectToDevice();
    assertEq(ok, false, "connect should not succeed when no device is picked");
    assertEq(controller.getSnapshot().state, "disconnected", "controller state");
    assertEq(controller.getSnapshot().device, null, "stale device should clear");
  });

  await test("send file marks the session degraded after reset_unavailable", async () => {
    const host = new FakeHost();
    host.warningResponses.push("Send Anyway");
    const client = new FakeClient();
    client.resetImpl = async () => {
      throw new ControlSessionClientError({
        code: "reset_unavailable",
        message: "connected Frothy kernel does not support control reset",
      });
    };

    const controller = createController(host, client);
    await controller.connectToDevice();
    await controller.sendFile();
    assertEq(controller.getSnapshot().degradedSendFile, true, "degraded flag");

    client.resetImpl = async () => ({});
    await controller.sendFile();
    assertEq(controller.getSnapshot().degradedSendFile, false, "degraded cleared");
  });

  await test("helper exit drops the session back to disconnected", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    const controller = createController(host, client);
    await controller.connectToDevice();
    client.exit(new Error("helper died"));
    assertEq(controller.getSnapshot().state, "disconnected", "exit state");
    assert(
      host.output.buffer.includes("helper exited: helper died"),
      "helper exit log",
    );
  });

  await test("send file handles helper exit between top-level forms", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    const controller = createController(host, client, {
      sendSource: "keep = 1\ndrop = 2\n",
    });

    let evalCount = 0;
    client.evalImpl = async () => {
      evalCount += 1;
      if (evalCount === 1) {
        client.exit(null);
      }
      return { text: "nil" };
    };

    await controller.connectToDevice();
    await controller.sendFile();
    assertEq(evalCount, 1, "send file should stop after helper exit");
    assertEq(controller.getSnapshot().state, "disconnected", "helper exit should disconnect");
    assert(
      host.warningMessages.some((entry) => /No Frothy device connected/.test(entry.message)),
      "disconnect warning",
    );
  });

  process.stdout.write(`\npassed ${passed}\n`);
  if (failed > 0) {
    process.stdout.write(`failed ${failed}\n`);
    for (const failure of failures) {
      process.stdout.write(`- ${failure.name}: ${failure.err.stack}\n`);
    }
    process.exit(1);
  }
}

main().catch((err) => {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
});
