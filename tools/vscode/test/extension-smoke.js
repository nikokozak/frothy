#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const {
  ControlSessionClientError,
} = require("../out/control-session-client");
const {
  prepareSendFileReset,
  resetLogLine,
  resetUnavailableError,
  resetUnavailableLogLine,
} = require("../out/send-file-reset");

const manifest = JSON.parse(
  fs.readFileSync(path.join(__dirname, "..", "package.json"), "utf8"),
);

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function commandIds() {
  return new Set(manifest.contributes.commands.map((entry) => entry.command));
}

async function main() {
  process.stdout.write("\n=== Frothy extension manifest smoke tests ===\n\n");

  const commands = commandIds();
  for (const command of [
    "frothy.connect",
    "frothy.disconnect",
    "frothy.sendSelection",
    "frothy.sendFile",
    "frothy.interrupt",
    "frothy.words",
    "frothy.see",
    "frothy.core",
    "frothy.slotInfo",
    "frothy.save",
    "frothy.restore",
    "frothy.wipe",
    "frothy.doctor",
    "frothy.showConsole",
  ]) {
    assert(commands.has(command), `missing command ${command}`);
  }

  for (const command of ["froth.connect", "froth.tryLocal", "froth.reset"]) {
    assert(!commands.has(command), `stale public command ${command}`);
  }

  const config = manifest.contributes.configuration.properties;
  assert(config["frothy.cliPath"], "missing frothy.cliPath setting");
  assert(config["frothy.port"], "missing frothy.port setting");
  assert(!config["froth.cliPath"], "stale froth.cliPath setting");
  assert(!config["froth.localRuntimePath"], "stale local runtime setting");
  assert(manifest.repository, "missing repository metadata");
  assert(manifest.license === "MIT", "license should be MIT");
  assert(Array.isArray(manifest.files), "missing packaged files allowlist");
  assert(manifest.files.includes("README.md"), "README.md should ship in the VSIX");
  assert(manifest.files.includes("LICENSE"), "LICENSE should ship in the VSIX");

  const language = manifest.contributes.languages[0];
  assert(language.id === "frothy", "language id should be frothy");
  assert(language.extensions.includes(".frothy"), "missing .frothy extension");
  assert(language.extensions.includes(".froth"), "missing .froth compatibility extension");

  const welcome = manifest.contributes.viewsWelcome[0].contents;
  assert(/Connect Device/.test(welcome), "welcome content should offer connect");
  assert(!/Try Local/.test(welcome), "welcome content should not offer local mode");

  const output = [];
  const handledErrors = [];
  await prepareSendFileReset(
    { reset: async () => ({}) },
    { appendLine: (line) => output.push(line) },
    async () => {
      throw new Error("warning should not be shown for successful reset");
    },
    (label, err) => handledErrors.push({ label, err }),
  );
  assert(output.includes(resetLogLine), "successful reset should log reset");
  assert(handledErrors.length === 0, "successful reset should not handle errors");

  const staleOutput = [];
  const staleResult = await prepareSendFileReset(
    {
      reset: async () => {
        throw new ControlSessionClientError({
          code: "reset_unavailable",
          message: "connected Frothy kernel does not support control reset",
        });
      },
    },
    { appendLine: (line) => staleOutput.push(line) },
    async (message) => {
      assert(message === resetUnavailableError, "stale error text");
      return undefined;
    },
    () => {
      throw new Error("stale reset should not route through generic error handler");
    },
  );
  assert(staleResult.proceed === false, "stale reset should block whole-file send");
  assert(staleResult.degraded === false, "stale reset should not mark degraded mode");
  assert(
    staleOutput.includes(resetUnavailableLogLine),
    "stale reset should log whole-file send abort",
  );

  process.stdout.write("passed manifest smoke\n");
}

try {
  main();
} catch (err) {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
}
