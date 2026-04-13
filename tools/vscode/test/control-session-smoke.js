#!/usr/bin/env node
"use strict";

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

  await test("CLI discovery still prefers froth and repo-local paths", async () => {
    const cwd = path.resolve(__dirname, "..", "..");
    const candidates = cliCandidates(cwd);
    assert(candidates.includes("froth"), "froth candidate");
    assert(
      candidates.some((candidate) => candidate.endsWith(path.join("tools", "cli", "froth"))),
      "repo-local candidate",
    );
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
