#!/usr/bin/env node
"use strict";

let buffer = "";
let pendingEval = null;
let supportsReset = true;
let connectedPort = "/dev/cu.mock";

process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
  buffer += chunk;

  for (;;) {
    const index = buffer.indexOf("\n");
    if (index < 0) {
      return;
    }

    const line = buffer.slice(0, index);
    buffer = buffer.slice(index + 1);
    if (line.length === 0) {
      continue;
    }

    handle(JSON.parse(line));
  }
});

function emit(message) {
  process.stdout.write(JSON.stringify(message) + "\n");
}

function respond(id, ok, result, error) {
  emit({ type: "response", id, ok, result, error });
}

function handle(message) {
  switch (message.command) {
    case "connect":
      connectedPort = message.port || "/dev/cu.mock";
      supportsReset = !connectedPort.includes("stale");
      emit({
        type: "event",
        event: "connected",
        request_id: message.id,
        device: {
          port: connectedPort,
          board: "mock-board",
          version: supportsReset ? "0.1.0-test" : "0.1.0-stale",
          cell_bits: 32,
          max_payload: 256
        }
      });
      respond(message.id, true, {
        port: connectedPort,
        board: "mock-board",
        version: supportsReset ? "0.1.0-test" : "0.1.0-stale",
        cell_bits: 32,
        max_payload: 256
      });
      return;
    case "disconnect":
      supportsReset = true;
      connectedPort = "/dev/cu.mock";
      emit({ type: "event", event: "disconnected", request_id: message.id });
      respond(message.id, true, null);
      return;
    case "interrupt":
      respond(message.id, true, null);
      if (pendingEval) {
        emit({ type: "event", event: "interrupted", request_id: pendingEval.id });
        emit({ type: "event", event: "idle", request_id: pendingEval.id });
        respond(pendingEval.id, false, null, {
          code: "interrupted",
          message: "control request interrupted"
        });
        pendingEval = null;
      }
      return;
    case "eval":
      if (message.source === "loop") {
        emit({
          type: "event",
          event: "output",
          request_id: message.id,
          data: Buffer.from("running\n", "utf8").toString("base64")
        });
        pendingEval = { id: message.id };
        return;
      }
      emit({
        type: "event",
        event: "value",
        request_id: message.id,
        value: { text: message.source === "1 + 1" ? "2" : "nil" }
      });
      emit({ type: "event", event: "idle", request_id: message.id });
      respond(message.id, true, { text: message.source === "1 + 1" ? "2" : "nil" });
      return;
    case "reset":
      if (supportsReset) {
        const value = {
          status: 0,
          heap_size: 65536,
          heap_used: 128,
          heap_overlay_used: 0,
          slot_count: 8,
          slot_overlay_count: 0,
          flags: 0,
          version: "0.1.0-test"
        };
        emit({
          type: "event",
          event: "value",
          request_id: message.id,
          value
        });
        emit({ type: "event", event: "idle", request_id: message.id });
        respond(message.id, true, value);
        return;
      }
      emit({
        type: "event",
        event: "error",
        request_id: message.id,
        error: {
          code: "reset_unavailable",
          message: "connected Frothy kernel does not support control reset"
        }
      });
      emit({ type: "event", event: "idle", request_id: message.id });
      respond(message.id, false, null, {
        code: "reset_unavailable",
        message: "connected Frothy kernel does not support control reset"
      });
      return;
    case "words":
      emit({
        type: "event",
        event: "value",
        request_id: message.id,
        value: { words: ["save", "restore", "control.demo"] }
      });
      emit({ type: "event", event: "idle", request_id: message.id });
      respond(message.id, true, { words: ["save", "restore", "control.demo"] });
      return;
    case "see":
      if (message.name === "missing") {
        emit({
          type: "event",
          event: "error",
          request_id: message.id,
          error: {
            code: "control_error",
            message: "undefined name",
            phase: 4,
            detail_code: 9
          }
        });
        emit({ type: "event", event: "idle", request_id: message.id });
        respond(message.id, false, null, {
          code: "control_error",
          message: "undefined name",
          phase: 4,
          detail_code: 9
        });
        return;
      }
      emit({
        type: "event",
        event: "value",
        request_id: message.id,
        value: {
          name: message.name,
          is_overlay: true,
          value_class: 5,
          rendered: "42"
        }
      });
      emit({ type: "event", event: "idle", request_id: message.id });
      respond(message.id, true, {
        name: message.name,
        is_overlay: true,
        value_class: 5,
        rendered: "42"
      });
      return;
    case "save":
    case "restore":
    case "wipe":
    case "core":
    case "slot_info":
      emit({
        type: "event",
        event: "value",
        request_id: message.id,
        value: { text: "nil" }
      });
      emit({ type: "event", event: "idle", request_id: message.id });
      respond(message.id, true, { text: "nil" });
      return;
    default:
      respond(message.id, false, null, {
        code: "unknown_command",
        message: `unknown command: ${message.command}`
      });
  }
}
