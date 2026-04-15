#!/usr/bin/env node
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  ControlSessionClient,
  ControlSessionClientError,
} = require("../out/control-session-client");
const {
  cliCandidates,
  resolveCliCandidate,
} = require("../out/cli-discovery");
const { resolveSendSourceCommand } = require("../out/send-file");

let passed = 0;
let failed = 0;
const failures = [];

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function assertEq(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

async function assertRejects(promise, msg) {
  try {
    await promise;
    throw new Error("expected rejection");
  } catch (err) {
    if (err.message === "expected rejection") {
      throw new Error(msg);
    }
    return err;
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

async function main() {
  process.stdout.write("\n=== Frothy control-session smoke tests ===\n\n");

  await test("client talks to mock helper over stdio", async () => {
    const helperPath = path.join(__dirname, "mock-control-session.js");
    const client = new ControlSessionClient(process.execPath, process.cwd(), [
      helperPath,
    ]);
    const events = [];
    client.onEvent((event) => events.push(event));

    await client.start();
    const device = await client.connect();
    assertEq(device.board, "mock-board", "device.board");

    const evalResult = await client.eval("1 + 1");
    assertEq(evalResult.text, "2", "eval result");

    const words = await client.words();
    assert(words.words.includes("control.demo"), "words result");

    const see = await client.see("control.demo");
    assertEq(see.rendered, "42", "see result");

    const core = await client.core("save");
    assertEq(core.text, "nil", "core result");

    const slotInfo = await client.slotInfo("save");
    assertEq(slotInfo.text, "nil", "slotInfo result");

    const reset = await client.reset();
    assert(reset.heap_size > 0, "reset heap size");
    assertEq(reset.version, "0.1.0-test", "reset version");

    const loopPromise = client.eval("loop");
    await new Promise((resolve) => setTimeout(resolve, 20));
    await client.interrupt();
    const interruptErr = await assertRejects(loopPromise, "loop should reject");
    assert(interruptErr instanceof ControlSessionClientError, "interrupt error type");
    assertEq(interruptErr.code, "interrupted", "interrupt code");

    await client.disconnect();
    client.dispose();

    assert(events.some((event) => event.event === "connected"), "connected event");
    assert(events.some((event) => event.event === "output"), "output event");
    assert(events.some((event) => event.event === "interrupted"), "interrupted event");
    assert(events.some((event) => event.event === "disconnected"), "disconnected event");
    const outputText = events
      .filter((event) => event.event === "output" && event.data)
      .map((event) => Buffer.from(event.data, "base64").toString("utf8"))
      .join("");
    assert(outputText.includes("core: <native save/0>"), "core output text");
    assert(outputText.includes("owner: runtime builtin"), "slotInfo output text");
  });

  await test("stale firmware keeps reset_unavailable compatibility", async () => {
    const helperPath = path.join(__dirname, "mock-control-session.js");
    const client = new ControlSessionClient(process.execPath, process.cwd(), [
      helperPath,
    ]);

    await client.start();
    await client.connect("/dev/cu.stale");
    const resetErr = await assertRejects(client.reset(), "reset should reject");
    assert(resetErr instanceof ControlSessionClientError, "reset error type");
    assertEq(resetErr.code, "reset_unavailable", "reset unavailable code");
    client.dispose();
  });

  await test("helper errors surface structured metadata", async () => {
    const helperPath = path.join(__dirname, "mock-control-session.js");
    const client = new ControlSessionClient(process.execPath, process.cwd(), [
      helperPath,
    ]);

    await client.start();
    await client.connect();
    const err = await assertRejects(client.see("missing"), "see should fail");
    assert(err instanceof ControlSessionClientError, "structured error type");
    assertEq(err.code, "control_error", "structured error code");
    assertEq(err.phase, 4, "structured error phase");
    client.dispose();
  });

  await test("client reconnects on the same helper-owned path", async () => {
    const helperPath = path.join(__dirname, "mock-control-session.js");
    const client = new ControlSessionClient(process.execPath, process.cwd(), [
      helperPath,
    ]);
    const connectedPorts = [];
    const disconnected = [];

    client.onEvent((event) => {
      if (event.event === "connected" && event.device) {
        connectedPorts.push(event.device.port);
      }
      if (event.event === "disconnected") {
        disconnected.push(true);
      }
    });

    await client.start();
    const firstDevice = await client.connect("/dev/cu.first");
    assertEq(firstDevice.port, "/dev/cu.first", "first connect port");
    await client.disconnect();

    const secondDevice = await client.connect("/dev/cu.second");
    assertEq(secondDevice.port, "/dev/cu.second", "second connect port");
    await client.disconnect();
    client.dispose();

    assertEq(connectedPorts.length, 2, "connected event count");
    assertEq(connectedPorts[0], "/dev/cu.first", "first connected event port");
    assertEq(connectedPorts[1], "/dev/cu.second", "second connected event port");
    assertEq(disconnected.length, 2, "disconnected event count");
  });

  await test("CLI discovery prefers frothy and keeps legacy fallback paths", async () => {
    const cwd = path.resolve(__dirname, "..", "..");
    const candidates = cliCandidates(cwd);
    assert(candidates.includes("frothy"), "frothy candidate");
    assert(candidates.includes("froth"), "froth candidate");
    assert(
      candidates.some((candidate) => candidate.endsWith(path.join("tools", "cli", "frothy-cli"))),
      "repo-local frothy-cli candidate",
    );
    assert(
      candidates.some((candidate) => candidate.endsWith(path.join("tools", "cli", "froth-cli"))),
      "repo-local legacy froth-cli candidate",
    );
    assert(
      candidates.some((candidate) => candidate.endsWith(path.join("tools", "cli", "frothy"))),
      "repo-local frothy candidate",
    );
    assert(
      candidates.some((candidate) => candidate.endsWith(path.join("tools", "cli", "froth"))),
      "repo-local legacy froth candidate",
    );
    assert(
      candidates.indexOf("frothy") < candidates.indexOf("froth"),
      "frothy comes before legacy froth",
    );
  });

  await test("CLI discovery picks repo-local frothy-cli before legacy PATH froth", async () => {
    const repoRoot = fs.mkdtempSync(path.join(os.tmpdir(), "frothy-cli-discovery-"));
    const pathDir = fs.mkdtempSync(path.join(os.tmpdir(), "frothy-cli-path-"));

    try {
      const repoCli = path.join(repoRoot, "tools", "cli", "frothy-cli");
      const legacyCli = path.join(pathDir, "froth");
      fs.mkdirSync(path.dirname(repoCli), { recursive: true });
      fs.writeFileSync(repoCli, "#!/bin/sh\n");
      fs.writeFileSync(legacyCli, "#!/bin/sh\n");

      const candidates = cliCandidates(repoRoot).filter((candidate) => ![
        "frothy",
        "/opt/homebrew/bin/frothy",
        "/usr/local/bin/frothy",
        "frothy-cli",
      ].includes(candidate));

      let resolved = null;
      for (const candidate of candidates) {
        resolved = resolveCliCandidate(candidate, repoRoot, pathDir);
        if (resolved) {
          break;
        }
      }

      assertEq(resolved, repoCli, "repo-local frothy-cli wins before legacy PATH froth");
    } finally {
      fs.rmSync(repoRoot, { recursive: true, force: true });
      fs.rmSync(pathDir, { recursive: true, force: true });
    }
  });

  await test("resolveCliCandidate finds repo-local files", async () => {
    const cwd = path.resolve(__dirname, "..", "..");
    const candidate = path.resolve(__dirname, "..", "package.json");
    const resolved = resolveCliCandidate(candidate, cwd);
    assert(resolved && resolved.endsWith("package.json"), "resolved repo file");
  });

  await test("send-file helper reports Frothy wording for old CLI failures", async () => {
    const err = await assertRejects(
      resolveSendSourceCommand("missing-cli", "/tmp/demo.frothy", process.cwd(), async () => {
        const failure = new Error("missing");
        failure.code = "ENOENT";
        throw failure;
      }),
      "missing cli should fail",
    );
    assert(/Frothy CLI not found/.test(err.message), "send-file error wording");
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
