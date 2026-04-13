#!/usr/bin/env python3
import argparse
import os
import pty
import re
import select
import shlex
import signal
import subprocess
import sys
import time


ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TARGET_DIR = os.path.join(ROOT_DIR, "targets", "esp-idf")
PROMPT = b"frothy> "


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_contains(text: str, needle: str) -> None:
    if needle not in text:
        fail(f"expected transcript to contain: {needle}")


def require_match(text: str, pattern: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        fail(f"expected transcript to match: {pattern}")


class IdfMonitorSession:
    def __init__(self, port: str) -> None:
        self.master_fd, slave_fd = pty.openpty()
        command = (
            '. "$HOME/.froth/sdk/esp-idf/export.sh" >/dev/null 2>&1 && '
            f"idf.py -p {shlex.quote(port)} flash monitor"
        )
        self.proc = subprocess.Popen(
            ["/bin/bash", "-lc", command],
            cwd=TARGET_DIR,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            start_new_session=True,
        )
        os.close(slave_fd)
        self.transcript = bytearray()

    def close(self) -> None:
        try:
            if self.proc.poll() is None:
                try:
                    os.write(self.master_fd, b"\x1d")
                except OSError:
                    pass
                try:
                    self.proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                    self.proc.wait(timeout=3.0)
        finally:
            os.close(self.master_fd)

    def read_until(self, needle: bytes, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        chunk = bytearray()

        while needle not in chunk:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                fail(f"timed out waiting for {needle!r}\n{self.text()}")

            ready, _, _ = select.select([self.master_fd], [], [], remaining)
            if not ready:
                continue

            data = os.read(self.master_fd, 4096)
            if not data:
                fail(f"monitor exited before {needle!r}\n{self.text()}")

            chunk.extend(data)
            self.transcript.extend(data)

        return bytes(chunk)

    def send_line(self, text: str, timeout: float = 5.0) -> None:
        os.write(self.master_fd, text.encode("utf-8") + b"\n")
        self.read_until(PROMPT, timeout)

    def text(self) -> str:
        return self.transcript.decode("utf-8", errors="replace").replace(
            "\r\n", "\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Flash and verify the Frothy M9 ESP32 board FFI surface."
    )
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbserial-0001")
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=90.0,
        help="Seconds to wait for the first Frothy prompt after flash.",
    )
    args = parser.parse_args()

    session = IdfMonitorSession(args.port)
    try:
        session.read_until(PROMPT, timeout=args.startup_timeout)
        session.send_line("gpio.mode(LED_BUILTIN, 1)")
        session.send_line("gpio.write(LED_BUILTIN, 1)")
        session.send_line("gpio.write(LED_BUILTIN, 0)")
        session.send_line("ms(5)")
        session.send_line("adc.read(A0)")

        transcript = session.text()
        print(transcript, end="")
        require_contains(transcript, "Frothy shell")
        require_contains(transcript, "boot: CTRL-C for safe boot")
        require_contains(transcript, "gpio.mode(LED_BUILTIN, 1)")
        require_contains(transcript, "gpio.write(LED_BUILTIN, 1)")
        require_contains(transcript, "gpio.write(LED_BUILTIN, 0)")
        require_contains(transcript, "ms(5)")
        require_contains(transcript, "adc.read(A0)")
        require_match(transcript, r"adc\.read\(A0\)\n\d+\nfrothy> ")
    finally:
        session.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
