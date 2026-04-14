#!/usr/bin/env node
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");

const root = path.join(__dirname, "..");
const manifest = JSON.parse(
  fs.readFileSync(path.join(root, "package.json"), "utf8"),
);

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function vscePath() {
  const binary = process.platform === "win32" ? "vsce.cmd" : "vsce";
  return path.join(root, "node_modules", ".bin", binary);
}

function listVsixEntries(vsixPath) {
  const output = execFileSync("unzip", ["-Z1", vsixPath], {
    cwd: root,
    encoding: "utf8",
  });
  return output
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
}

function includesNormalizedPath(entries, expected) {
  const normalized = expected.toLowerCase();
  return entries.some((entry) => entry.toLowerCase() === normalized);
}

function includesPrefix(entries, prefix) {
  return entries.some((entry) => entry.startsWith(prefix));
}

function main() {
  process.stdout.write("\n=== Frothy VSIX package smoke tests ===\n\n");

  assert(manifest.repository, "missing repository metadata");
  assert(manifest.license === "MIT", "license should be MIT");
  assert(Array.isArray(manifest.files), "missing packaged files allowlist");

  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "frothy-vsix-"));
  const vsixFile = path.join(tempDir, "frothy-package-smoke.vsix");

  try {
    execFileSync(vscePath(), ["package", "--no-yarn", "--out", vsixFile], {
      cwd: root,
      stdio: "pipe",
    });

    assert(fs.existsSync(vsixFile), "expected VSIX output");
    const entries = listVsixEntries(vsixFile);

    for (const required of [
      "extension/package.json",
      "extension/readme.md",
      "extension/license.txt",
      "extension/language-configuration.json",
      "extension/out/cli-discovery.js",
      "extension/out/control-session-client.js",
      "extension/out/extension.js",
      "extension/out/send-file.js",
      "extension/out/send-file-reset.js",
      "extension/syntaxes/froth.tmLanguage.json",
    ]) {
      assert(
        includesNormalizedPath(entries, required),
        `missing VSIX entry ${required}`,
      );
    }

    for (const forbidden of [
      "extension/tsconfig.json",
      "extension/package-lock.json",
      "extension/out/daemon-client.js",
      "extension/out/daemon-supervisor.js",
      "extension/out/froth-paths.js",
    ]) {
      assert(
        !includesNormalizedPath(entries, forbidden),
        `unexpected VSIX entry ${forbidden}`,
      );
    }

    for (const forbiddenPrefix of [
      "extension/src/",
      "extension/test/",
      "extension/node_modules/",
    ]) {
      assert(
        !includesPrefix(entries, forbiddenPrefix),
        `unexpected VSIX path prefix ${forbiddenPrefix}`,
      );
    }

    assert(
      !entries.some((entry) => entry.startsWith("extension/out/") && entry.endsWith(".map")),
      "unexpected source map in VSIX",
    );

    process.stdout.write("passed package smoke\n");
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
}

try {
  main();
} catch (err) {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
}
