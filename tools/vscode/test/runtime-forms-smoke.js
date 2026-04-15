#!/usr/bin/env node
"use strict";

const {
  findTopLevelFormAtLine,
  splitTopLevelForms,
} = require("../out/runtime-forms");

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
    splitTopLevelForms("keep is 10\ndrop is 20\n"),
    ["keep is 10", "drop is 20"],
    "multiple top-level bindings",
  );

  assertArrayEq(
    splitTopLevelForms("to probe\n[ 1 + 1 ]\nkeep is 30\n"),
    ["to probe\n[ 1 + 1 ]", "keep is 30"],
    "block form plus binding",
  );

  assertArrayEq(
    splitTopLevelForms("\\ comment\nkeep is 30\n"),
    ["keep is 30"],
    "line comments skipped",
  );

  assertThrows(
    () => splitTopLevelForms("to boot\n"),
    /incomplete Frothy source form/,
    "incomplete form should fail",
  );

  assertArrayEq(
    splitTopLevelForms(
      "keep is if true [\n  1\n]\nelse [\n  2\n]\nafter is 7\n",
    ),
    ["keep is if true [\n  1\n]\nelse [\n  2\n]", "after is 7"],
    "multiline if/else stays one form",
  );

  assertArrayEq(
    splitTopLevelForms("keep is if true [\n  1\n]\nelsewhere is 2\n"),
    ["keep is if true [\n  1\n]", "elsewhere is 2"],
    "else-prefixed names do not join prior if",
  );

  assertArrayEq(
    splitTopLevelForms(
      "to tm1629.brightness! with level\n[ tm1629.raw.brightness!: level ]\n",
    ),
    ["to tm1629.brightness! with level\n[ tm1629.raw.brightness!: level ]"],
    "punctuated names stay in multiline headers",
  );

  const source = [
    "to demo.pong.setup [",
    "  matrix.init:;",
    "  matrix.brightness!: 1;",
    "]",
    "",
    "matrix.brightness!: 2;",
  ].join("\n");
  assertEq(
    findTopLevelFormAtLine(source, 0),
    "to demo.pong.setup [\n  matrix.init:;\n  matrix.brightness!: 1;\n]",
    "header line resolves full form",
  );
  assertEq(
    findTopLevelFormAtLine(source, 2),
    "to demo.pong.setup [\n  matrix.init:;\n  matrix.brightness!: 1;\n]",
    "body line resolves full form",
  );
  assertEq(
    findTopLevelFormAtLine(source, 5),
    "matrix.brightness!: 2;",
    "single-line form resolves as-is",
  );

  process.stdout.write("passed runtime-form smoke\n");
}

try {
  main();
} catch (err) {
  process.stderr.write(String(err.stack || err) + "\n");
  process.exit(1);
}
