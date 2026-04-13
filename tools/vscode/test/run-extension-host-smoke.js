#!/usr/bin/env node
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { runTests } = require("@vscode/test-electron");

function parseArgs(argv) {
  const out = {
    mode: "local",
    workspace: path.resolve(__dirname, "fixtures", "editor-smoke"),
    cliPath: "",
    realCliPath: "",
    localRuntime: "",
    port: "",
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    switch (arg) {
      case "--mode":
        out.mode = argv[++i];
        break;
      case "--workspace":
        out.workspace = path.resolve(argv[++i]);
        break;
      case "--cli-path":
        out.cliPath = path.resolve(argv[++i]);
        break;
      case "--real-cli-path":
        out.realCliPath = path.resolve(argv[++i]);
        break;
      case "--local-runtime":
        out.localRuntime = path.resolve(argv[++i]);
        break;
      case "--port":
        out.port = argv[++i];
        break;
      default:
        throw new Error(`unknown flag: ${arg}`);
    }
  }

  if (!out.cliPath) {
    throw new Error("--cli-path is required");
  }
  if (out.mode !== "local" && out.mode !== "serial") {
    throw new Error(`unsupported mode: ${out.mode}`);
  }
  if (out.mode === "local" && !out.localRuntime) {
    throw new Error("--local-runtime is required in local mode");
  }
  if (out.mode === "local" && !out.realCliPath) {
    throw new Error("--real-cli-path is required in local mode");
  }
  if (out.mode === "serial" && !out.port) {
    throw new Error("--port is required in serial mode");
  }
  return out;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const extensionDevelopmentPath = path.resolve(__dirname, "..");
  const extensionTestsPath = path.resolve(__dirname, "extension-host", "index.js");
  const profilePath = fs.mkdtempSync(path.join(os.tmpdir(), "frothy-vscode-smoke-"));

  await runTests({
    extensionDevelopmentPath,
    extensionTestsPath,
    launchArgs: [
      options.workspace,
      `--extensions-dir=${path.join(profilePath, "extensions")}`,
      `--user-data-dir=${path.join(profilePath, "user-data")}`,
      "--disable-extensions",
      "--disable-workspace-trust",
    ],
    extensionTestsEnv: {
      FROTHY_VSCODE_SMOKE_MODE: options.mode,
      FROTHY_VSCODE_SMOKE_CLI_PATH: options.cliPath,
      FROTHY_VSCODE_SMOKE_REAL_CLI: options.realCliPath,
      FROTHY_VSCODE_SMOKE_LOCAL_RUNTIME: options.localRuntime,
      FROTHY_VSCODE_SMOKE_PORT: options.port,
    },
  });
}

main().catch((err) => {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
});
