#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
CLI_BIN="${FROTHY_CLI_BINARY:-$ROOT_DIR/tools/cli/frothy-cli}"
RUN_LIVE_CONTROLS=1
PORT=

usage() {
  echo "usage: $0 [--skip-live-controls] <PORT>" >&2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --live-controls)
      RUN_LIVE_CONTROLS=1
      shift
      ;;
    --skip-live-controls)
      RUN_LIVE_CONTROLS=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      usage
      exit 2
      ;;
    *)
      PORT=$1
      shift
      ;;
  esac
done

if [ -z "$PORT" ] || [ "$#" -ne 0 ]; then
  usage
  exit 2
fi

if [ ! -x "$CLI_BIN" ]; then
  echo "error: missing CLI binary: $CLI_BIN" >&2
  exit 1
fi

if [ ! -e "$PORT" ]; then
  echo "error: serial port is missing: $PORT" >&2
  exit 1
fi

python3 - "$CLI_BIN" "$PORT" "$RUN_LIVE_CONTROLS" <<'PY'
import os
import pty
import select
import signal
import subprocess
import sys
import time

PROMPT = b"frothy> "
CLI_BIN = sys.argv[1]
PORT = sys.argv[2]
RUN_LIVE_CONTROLS = sys.argv[3] == "1"


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


class ConnectSession:
    def __init__(self, cli_bin: str, port: str) -> None:
        self.master_fd, slave_fd = pty.openpty()
        self.proc = subprocess.Popen(
            [cli_bin, "--port", port, "connect"],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            start_new_session=True,
        )
        os.close(slave_fd)
        self._read_until_prompt(15.0)

    def close(self) -> None:
        try:
            if self.proc.poll() is None:
                try:
                    os.write(self.master_fd, b"quit\n")
                except OSError:
                    pass
                try:
                    self.proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(self.proc.pid, signal.SIGKILL)
                    except (PermissionError, ProcessLookupError):
                        self.proc.kill()
                    try:
                        self.proc.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        pass
        finally:
            try:
                os.close(self.master_fd)
            except OSError:
                pass

    def _read_until_prompt(self, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        chunk = bytearray()
        while PROMPT not in chunk:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                fail(f"timed out waiting for prompt; partial output:\n{chunk.decode(errors='replace')}")
            ready, _, _ = select.select([self.master_fd], [], [], remaining)
            if not ready:
                continue
            data = os.read(self.master_fd, 4096)
            if not data:
                fail("connect session exited before prompt")
            chunk.extend(data)
        return chunk.decode(errors="replace").replace("\r", "")

    def command(self, text: str, timeout: float = 15.0) -> str:
        os.write(self.master_fd, text.encode("utf-8") + b"\n")
        raw = self._read_until_prompt(timeout)
        if raw.startswith(text + "\n"):
            raw = raw[len(text) + 1 :]
        prompt_index = raw.rfind("frothy> ")
        if prompt_index >= 0:
            raw = raw[:prompt_index]
        return raw.strip()


def expect_exact(session: ConnectSession, label: str, expr: str, expected: str) -> None:
    actual = session.command(expr)
    if actual != expected:
        fail(f"{label} expected {expected!r}, got {actual!r}")


def expect_range(session: ConnectSession, label: str, expr: str, minimum: int, maximum: int) -> None:
    actual_text = session.command(expr)
    try:
        actual = int(actual_text)
    except ValueError as exc:
        raise SystemExit(f"error: {label} expected integer output, got {actual_text!r}") from exc
    if actual < minimum or actual > maximum:
        fail(f"{label} expected range [{minimum}, {maximum}], got {actual}")


def wait_for_exact(session: ConnectSession, label: str, expr: str, expected: str, timeout: float = 8.0) -> None:
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = session.command(expr)
        if last == expected:
            return
        time.sleep(0.2)
    fail(f"{label} expected {expected!r}, got {last!r}")


def expect_live_transition(session: ConnectSession, label: str, expr: str, active: str, inactive: str) -> None:
    print(f"{label}: hold the control now", file=sys.stderr, flush=True)
    wait_for_exact(session, f"{label} active", expr, active, timeout=30.0)
    print(f"{label}: release the control now", file=sys.stderr, flush=True)
    wait_for_exact(session, f"{label} released", expr, inactive, timeout=30.0)


session = ConnectSession(CLI_BIN, PORT)
try:
    session.command("matrix.init:")
    session.command("grid.clear:")
    session.command("grid.fill: true")
    expect_exact(session, "row0 after grid.fill true", "tm1629.raw.row@: 0", "4095")
    session.command("grid.fill: false")
    expect_exact(session, "row0 after grid.fill false", "tm1629.raw.row@: 0", "0")
    session.command("grid.set: 1, 1, true")
    session.command("grid.show:")
    expect_exact(session, "row1 after single pixel", "tm1629.raw.row@: 1", "2")
    session.command("grid.rect: 2, 2, 4, 3, true")
    session.command("grid.show:")
    expect_exact(session, "row2 after rect", "tm1629.raw.row@: 2", "60")

    expect_range(session, "knob.left.raw", "knob.left.raw:", 0, 4095)
    expect_range(session, "knob.right.raw", "knob.right.raw:", 0, 4095)
    expect_range(session, "knob.left", "knob.left:", 0, 100)
    expect_range(session, "knob.right", "knob.right:", 0, 100)

    expect_exact(session, "joy.up.pin", "joy.up.pin", "17")
    expect_exact(session, "joy.down.pin", "joy.down.pin", "16")
    expect_exact(session, "joy.left.pin", "joy.left.pin", "13")
    expect_exact(session, "joy.right.pin", "joy.right.pin", "14")
    expect_exact(session, "joy.click.pin", "joy.click.pin", "25")

    expect_exact(session, "joy.up? idle", "joy.up?:", "false")
    expect_exact(session, "joy.down? idle", "joy.down?:", "false")
    expect_exact(session, "joy.left? idle", "joy.left?:", "false")
    expect_exact(session, "joy.right? idle", "joy.right?:", "false")
    expect_exact(session, "joy.click? idle", "joy.click?:", "false")

    session.command("joy.up.pin is LED_BUILTIN")
    expect_exact(session, "overlay joy.up.pin", "joy.up.pin", "2")
    session.command("dangerous.wipe:")
    expect_exact(session, "restored joy.up.pin", "joy.up.pin", "17")
    expect_exact(session, "restored joy.down.pin", "joy.down.pin", "16")
    expect_exact(session, "restored joy.left.pin", "joy.left.pin", "13")
    expect_exact(session, "restored joy.right.pin", "joy.right.pin", "14")
    expect_exact(session, "restored joy.click.pin", "joy.click.pin", "25")
    expect_exact(session, "restored knob.left.pin", "knob.left.pin", "33")
    expect_exact(session, "restored knob.right.pin", "knob.right.pin", "32")
    expect_range(session, "knob.left after wipe", "knob.left:", 0, 100)
    expect_range(session, "knob.right after wipe", "knob.right:", 0, 100)

    if RUN_LIVE_CONTROLS:
        expect_live_transition(session, "joy.up?", "joy.up?:", "true", "false")
        expect_live_transition(session, "joy.down?", "joy.down?:", "true", "false")
        expect_live_transition(session, "joy.left?", "joy.left?:", "true", "false")
        expect_live_transition(session, "joy.right?", "joy.right?:", "true", "false")
        expect_live_transition(session, "joy.click?", "joy.click?:", "true", "false")
finally:
    session.close()

print(f"ok: v4 workshop surface smoke passed on {PORT}")
PY
