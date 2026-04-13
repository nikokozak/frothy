#!/usr/bin/env node
"use strict";

const { splitTopLevelForms } = require("../out/runtime-forms");

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

function assertArrayEq(actual, expected, msg) {
  assertEq(JSON.stringify(actual), JSON.stringify(expected), msg);
}

function assertThrows(fn, pattern, msg) {
  try {
    fn();
    throw new Error("expected throw");
  } catch (err) {
    if (err.message === "expected throw") {
      throw new Error(msg);
    }
    if (!pattern.test(err.message)) {
      throw new Error(`${msg}: got ${err.message}`);
    }
  }
}

function main() {
  process.stdout.write("\n=== Frothy runtime-form smoke tests ===\n\n");

  assertArrayEq(
    splitTopLevelForms("1 + 2\n"),
    ["1 + 2"],
    "single form",
  );

  assertArrayEq(
    splitTopLevelForms("keep = 10\ndrop = 20\n"),
    ["keep = 10", "drop = 20"],
    "multiple top-level assignments",
  );

  assertArrayEq(
    splitTopLevelForms("probe(n) {\n  n + 1\n}\nkeep = 30\n"),
    ["probe(n) {\n  n + 1\n}", "keep = 30"],
    "block form plus assignment",
  );

  assertArrayEq(
    splitTopLevelForms("\\ comment\nkeep = 30\n"),
    ["keep = 30"],
    "line comments skipped",
  );

  assertThrows(
    () => splitTopLevelForms("boot {\n"),
    /incomplete Frothy source form/,
    "incomplete form should fail",
  );

  assertArrayEq(
    splitTopLevelForms(
      "keep = if true {\n  1\n}\nelse {\n  2\n}\nafter = 7\n",
    ),
    ["keep = if true {\n  1\n}\nelse {\n  2\n}", "after = 7"],
    "multiline if/else stays one form",
  );

  assertArrayEq(
    splitTopLevelForms("keep = if true {\n  1\n}\nelsewhere = 2\n"),
    ["keep = if true {\n  1\n}", "elsewhere = 2"],
    "else-prefixed names do not join prior if",
  );

  process.stdout.write("passed runtime-form smoke\n");
}

try {
  main();
} catch (err) {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
}
