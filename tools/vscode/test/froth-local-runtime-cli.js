#!/usr/bin/env node
"use strict";

const { spawn } = require("child_process");
const path = require("path");

function fail(message) {
  process.stderr.write(`error: ${message}\n`);
  process.exit(1);
}

const realCli = process.env.FROTHY_TEST_REAL_CLI;
if (!realCli) {
  fail("FROTHY_TEST_REAL_CLI is required");
}

const args = process.argv.slice(2);
if (args.length === 0) {
  fail("expected Frothy CLI arguments");
}

const proxiedArgs = [...args];
if (args[0] === "tooling" && args[1] === "control-session") {
  const localRuntime = process.env.FROTHY_TEST_LOCAL_RUNTIME;
  if (!localRuntime) {
    fail("FROTHY_TEST_LOCAL_RUNTIME is required for control-session smoke");
  }
  proxiedArgs.push("--local-runtime", path.resolve(localRuntime));
}

const child = spawn(realCli, proxiedArgs, {
  stdio: "inherit",
  env: process.env,
});

child.on("error", (err) => {
  fail(err.message);
});

child.on("exit", (code, signal) => {
  if (signal) {
    process.kill(process.pid, signal);
    return;
  }
  process.exit(code ?? 1);
});
