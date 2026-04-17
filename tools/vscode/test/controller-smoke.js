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

async function waitFor(cond, msg) {
  for (let i = 0; i < 50; i++) {
    if (cond()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error(msg);
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
    this.formText = lineText;
    this.selectionValue = "";
    this.name = "probe";
  }

  selectionText() {
    return this.selectionValue;
  }

  currentLineText() {
    return this.lineText;
  }

  currentRuntimeFormText() {
    return this.formText;
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
    this.interruptImpl = async () => {};
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
    this.disconnectCalls = (this.disconnectCalls || 0) + 1;
    this.emit({ type: "event", event: "disconnected" });
  }

  async eval(source) {
    this.evalCalls = (this.evalCalls || []).concat([source]);
    return this.evalImpl(source);
  }

  async reset() {
    return this.resetImpl();
  }

  async interrupt() {
    this.interruptCalls = (this.interruptCalls || 0) + 1;
    return this.interruptImpl();
  }

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

function createController(host, clientOrClients, overrides = {}) {
  const clients = Array.isArray(clientOrClients)
    ? clientOrClients.slice()
    : [clientOrClients];
  const fallbackClient = clients[clients.length - 1];
  return new FrothyController({
    host,
    async resolveCliPath() {
      return "/tmp/froth";
    },
    createClient() {
      return clients.shift() || fallbackClient;
    },
    async resolveSendSource() {
      const source = typeof overrides.sendSource === "function"
        ? overrides.sendSource()
        : overrides.sendSource || "keep = 1";
      return { source };
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

  await test("stored last port is a soft hint and retries discovery", async () => {
    const host = new FakeHost();
    host.storedPort = "/dev/cu.stale";
    const client = new FakeClient();
    client.connectImpl = async (port) => {
      if (port === "/dev/cu.stale") {
        throw new ControlSessionClientError({
          code: "internal",
          message: "open serial port: no such file",
        });
      }
      return {
        port: "/dev/cu.live",
        board: "live",
        version: "0.1.0-test",
        cell_bits: 32,
      };
    };

    const controller = createController(host, client);
    const ok = await controller.connectToDevice();
    assertEq(ok, true, "connect should succeed after retry");
    assertEq(client.connectCalls.length, 2, "connect call count");
    assertEq(client.connectCalls[0], "/dev/cu.stale", "stale stored port");
    assertEq(client.connectCalls[1], "", "retry should use discovery");
    assertEq(host.storedPort, "/dev/cu.live", "stored port should refresh");
    assert(
      host.output.buffer.includes("remembered port failed"),
      "retry log",
    );
  });

  await test("send file blocks whole-file send after reset_unavailable", async () => {
    const host = new FakeHost();
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
    assertEq(controller.getSnapshot().degradedSendFile, false, "degraded flag");

    client.resetImpl = async () => ({});
    await controller.sendFile();
    assertEq(controller.getSnapshot().degradedSendFile, false, "degraded cleared");
  });

  await test("send selection uses the enclosing runtime form", async () => {
    const host = new FakeHost();
    host.editor.lineText = "to demo.pong.setup [";
    host.editor.formText = [
      "to demo.pong.setup [",
      "  matrix.init:;",
      "  matrix.brightness!: 1;",
      "]",
    ].join("\n");
    const client = new FakeClient();
    const controller = createController(host, client);

    await controller.connectToDevice();
    await controller.sendSelection();
    assertEq(client.evalCalls.length, 1, "send selection should eval once");
    assertEq(client.evalCalls[0], host.editor.formText, "send selection should eval full form");
  });

  await test("send selection keeps the previous run form across redefinitions", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    const controller = createController(host, client);

    await controller.connectToDevice();
    host.editor.lineText = "demo.loop:";
    host.editor.formText = "demo.loop:";
    await controller.sendSelection();
    assertEq(controller.getSnapshot().lastRunPreview, "demo.loop:", "last run preview");

    host.editor.lineText = "speed = 2";
    host.editor.formText = "speed = 2";
    await controller.sendSelection();
    host.editor.lineText = "demo.loop() { keep }";
    host.editor.formText = "demo.loop() { keep }";
    await controller.sendSelection();
    await controller.runLast();

    assertEq(client.evalCalls.length, 4, "eval call count");
    assertEq(client.evalCalls[0], "demo.loop:", "first run call");
    assertEq(client.evalCalls[1], "speed = 2", "redefinition call");
    assertEq(client.evalCalls[2], "demo.loop() { keep }", "function definition call");
    assertEq(client.evalCalls[3], "demo.loop:", "rerun call");
  });

  await test("run binding records a zero-arity call for rerun", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    const controller = createController(host, client);

    host.inputResponses.push("demo.loop");
    await controller.connectToDevice();
    await controller.runBinding();
    await controller.runLast();

    assertEq(client.evalCalls.length, 2, "eval call count");
    assertEq(client.evalCalls[0], "demo.loop:", "run binding call");
    assertEq(client.evalCalls[1], "demo.loop:", "rerun binding call");
  });

  await test("pinned run binding survives edits and separate last run changes", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    const controller = createController(host, client);

    host.inputResponses.push("boot");
    await controller.pinRunBinding();
    assertEq(controller.getSnapshot().pinnedRunPreview, "boot:", "pinned preview");

    await controller.connectToDevice();
    host.editor.lineText = "matrix.brightness!: 2;";
    host.editor.formText = "matrix.brightness!: 2;";
    await controller.sendSelection();
    await controller.runPinned();

    host.editor.lineText = "demo.once:";
    host.editor.formText = "demo.once:";
    await controller.sendSelection();
    assertEq(controller.getSnapshot().lastRunPreview, "demo.once:", "last run preview");
    assertEq(controller.getSnapshot().pinnedRunPreview, "boot:", "pinned should remain");
    await controller.runPinned();

    assertEq(client.evalCalls.length, 4, "eval call count");
    assertEq(client.evalCalls[0], "matrix.brightness!: 2;", "edit call");
    assertEq(client.evalCalls[1], "boot:", "first pinned call");
    assertEq(client.evalCalls[2], "demo.once:", "separate last run call");
    assertEq(client.evalCalls[3], "boot:", "second pinned call");
  });

  await test("wipe requires explicit confirmation", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    let wipeCalls = 0;
    client.wipe = async () => {
      wipeCalls += 1;
      return { text: "nil" };
    };

    const controller = createController(host, client);
    await controller.connectToDevice();

    await controller.wipeSnapshot();
    assertEq(wipeCalls, 0, "wipe should not run without confirmation");

    host.warningResponses.push("Wipe Snapshot");
    await controller.wipeSnapshot();
    assertEq(wipeCalls, 1, "wipe should run after confirmation");
  });

  await test("connect while running restarts the control session", async () => {
    const host = new FakeHost();
    const firstClient = new FakeClient();
    const secondClient = new FakeClient();
    let rejectEval = null;

    firstClient.evalImpl = async () => new Promise((_resolve, reject) => {
      rejectEval = reject;
    });
    firstClient.disconnect = async function disconnect() {
      this.disconnectCalls = (this.disconnectCalls || 0) + 1;
      this.emit({ type: "event", event: "disconnected" });
      if (rejectEval) {
        rejectEval(new ControlSessionClientError({
          code: "not_connected",
          message: "not connected",
        }));
      }
    };

    const controller = createController(host, [firstClient, secondClient]);
    await controller.connectToDevice();
    const runningEval = controller.sendSelection();
    await waitFor(
      () => controller.getSnapshot().state === "running",
      "controller did not enter running state",
    );

    const ok = await controller.connectToDevice();
    assertEq(ok, true, "reconnect should succeed");
    assertEq(firstClient.disconnectCalls, 1, "running client should disconnect");
    assertEq(secondClient.connectCalls.length, 1, "new helper should connect");
    assertEq(controller.getSnapshot().state, "connected", "controller state");
    assert(
      host.output.buffer.includes("reconnect requested while running"),
      "reconnect log",
    );

    firstClient.emit({ type: "event", event: "disconnected" });
    firstClient.exit(new Error("late old helper exit"));
    assertEq(
      controller.getSnapshot().state,
      "connected",
      "stale old helper events should not disconnect new session",
    );
    assert(
      !host.output.buffer.includes("late old helper exit"),
      "stale helper exit should not be logged",
    );

    await runningEval;
    assert(
      !host.warningMessages.some((entry) =>
        /No Frothy device connected/.test(entry.message)
      ),
      "intentional reconnect should not show stale disconnect warning",
    );
    assertEq(host.errorMessages.length, 0, "intentional reconnect errors");
  });

  await test("late old request completion does not disconnect new session", async () => {
    const host = new FakeHost();
    const firstClient = new FakeClient();
    const secondClient = new FakeClient();
    let rejectEval = null;

    firstClient.evalImpl = async () => new Promise((_resolve, reject) => {
      rejectEval = reject;
    });
    firstClient.disconnect = async function disconnect() {
      this.disconnectCalls = (this.disconnectCalls || 0) + 1;
      this.emit({ type: "event", event: "disconnected" });
    };

    const controller = createController(host, [firstClient, secondClient]);
    await controller.connectToDevice();
    const runningEval = controller.sendSelection();
    await waitFor(
      () => controller.getSnapshot().state === "running",
      "controller did not enter running state",
    );

    const ok = await controller.connectToDevice();
    assertEq(ok, true, "reconnect should succeed");
    assertEq(controller.getSnapshot().state, "connected", "controller state");

    rejectEval(new ControlSessionClientError({
      code: "not_connected",
      message: "not connected",
    }));
    await runningEval;

    assertEq(
      controller.getSnapshot().state,
      "connected",
      "late old request should not disconnect new session",
    );
    assert(
      !host.warningMessages.some((entry) =>
        /No Frothy device connected/.test(entry.message)
      ),
      "late old request should not warn as current disconnect",
    );
  });

  await test("slow old disconnect does not clear fresh reconnect", async () => {
    const host = new FakeHost();
    const firstClient = new FakeClient();
    const secondClient = new FakeClient();
    let markDisconnectStarted = null;
    const disconnectStarted = new Promise((resolve) => {
      markDisconnectStarted = resolve;
    });
    let releaseDisconnect = null;

    firstClient.disconnect = async function disconnect() {
      this.disconnectCalls = (this.disconnectCalls || 0) + 1;
      if (markDisconnectStarted) {
        markDisconnectStarted();
      }
      await new Promise((resolve) => {
        releaseDisconnect = resolve;
      });
      this.emit({ type: "event", event: "disconnected" });
    };

    const controller = createController(host, [firstClient, secondClient]);
    await controller.connectToDevice();
    const disconnecting = controller.disconnect();
    await disconnectStarted;

    const ok = await controller.connectToDevice();
    assertEq(ok, true, "fresh reconnect should succeed");
    assertEq(secondClient.connectCalls.length, 1, "second helper should connect");
    assertEq(controller.getSnapshot().state, "connected", "fresh state");

    releaseDisconnect();
    await disconnecting;

    assertEq(
      controller.getSnapshot().state,
      "connected",
      "old disconnect should not clear fresh session",
    );
    assert(controller.getSnapshot().device, "fresh device should remain");
  });

  await test("superseded connect rejection does not clear newer connect", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    let rejectFirstConnect = null;

    client.connectImpl = async (port) => {
      if ((client.connectCalls || []).length === 1) {
        return new Promise((_resolve, reject) => {
          rejectFirstConnect = reject;
        });
      }
      return {
        port: port || "/dev/cu.mock",
        board: "mock-board",
        version: "0.1.0-test",
        cell_bits: 32,
      };
    };

    const controller = createController(host, client);
    const firstConnect = controller.connectToDevice();
    await waitFor(
      () => (client.connectCalls || []).length === 1,
      "first connect did not start",
    );

    const secondConnect = controller.connectToDevice();
    assertEq(await secondConnect, true, "newer connect should succeed");
    assertEq(controller.getSnapshot().state, "connected", "newer connect state");

    rejectFirstConnect(new ControlSessionClientError({
      code: "not_connected",
      message: "connection attempt was superseded",
    }));
    assertEq(await firstConnect, true, "stale connect should not fail current state");
    assertEq(
      controller.getSnapshot().state,
      "connected",
      "stale connect rejection should not clear state",
    );
    assert(
      !host.warningMessages.some((entry) =>
        /No Frothy device connected/.test(entry.message)
      ),
      "stale connect should not warn",
    );
  });

  await test("send file interrupts and supersedes a running file send", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    let sendSource = "first = 1\nwhile true [ first ]\nold.tail = 1\n";
    let rejectRunningEval = null;
    let runningEvalStarted = false;

    client.evalImpl = async (source) => {
      if (source === "first = 1") {
        client.emit({ type: "event", event: "idle" });
        return { text: "nil" };
      }
      if (source === "while true [ first ]") {
        runningEvalStarted = true;
        return new Promise((_resolve, reject) => {
          rejectRunningEval = reject;
        });
      }
      return { text: "nil" };
    };
    client.interruptImpl = async () => {
      if (rejectRunningEval) {
        rejectRunningEval(new ControlSessionClientError({
          code: "interrupted",
          message: "control request interrupted",
        }));
      }
    };

    const controller = createController(host, client, {
      sendSource: () => sendSource,
    });
    await controller.connectToDevice();

    const firstSend = controller.sendFile();
    await waitFor(
      () => runningEvalStarted && controller.getSnapshot().state === "running",
      "first send did not stay running",
    );

    sendSource = "replacement = 2\n";
    await controller.sendFile();
    await firstSend;

    assertEq(client.interruptCalls, 1, "send file should interrupt old run");
    assert(
      client.evalCalls.includes("replacement = 2"),
      "replacement file should be sent after interrupt",
    );
    assert(
      !client.evalCalls.includes("old.tail = 1"),
      "old file tail should not run after interrupt",
    );
    assertEq(controller.getSnapshot().state, "connected", "controller state");
    assert(
      host.output.buffer.includes("send requested while running"),
      "send interrupt log",
    );
  });

  await test("send file aborts when helper changes during preflight", async () => {
    const host = new FakeHost();
    const client = new FakeClient();
    let resetCalls = 0;
    client.resetImpl = async () => {
      resetCalls += 1;
      return {};
    };

    const controller = createController(host, client, {
      sendSource: () => {
        client.exit(null);
        return "replacement = 2\n";
      },
    });
    await controller.connectToDevice();
    await controller.sendFile();

    assertEq(controller.getSnapshot().state, "disconnected", "helper exit state");
    assertEq(resetCalls, 0, "reset should not run on a changed helper");
    assertEq((client.evalCalls || []).length, 0, "file should not send");
    assert(
      !host.output.buffer.includes("[frothy] send "),
      "send log should not claim the file was sent",
    );
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
      !host.warningMessages.some((entry) => /No Frothy device connected/.test(entry.message)),
      "stale request completion should not emit disconnect warning",
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
