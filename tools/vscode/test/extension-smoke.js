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
  resetUnavailableAction,
  resetUnavailableLogLine,
  resetUnavailableWarning,
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
  const staleOk = await prepareSendFileReset(
    {
      reset: async () => {
        throw new ControlSessionClientError({
          code: "reset_unavailable",
          message: "connected Frothy kernel does not support control reset",
        });
      },
    },
    { appendLine: (line) => staleOutput.push(line) },
    async (message, action) => {
      assert(message === resetUnavailableWarning, "stale warning text");
      assert(action === resetUnavailableAction, "stale warning action");
      return action;
    },
    () => {
      throw new Error("stale reset should not route through generic error handler");
    },
  );
  assert(staleOk === true, "stale reset warning acceptance should continue send");
  assert(
    staleOutput.includes(resetUnavailableLogLine),
    "stale reset should log additive-send warning",
  );

  process.stdout.write("passed manifest smoke\n");
}

try {
  main();
} catch (err) {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
}
