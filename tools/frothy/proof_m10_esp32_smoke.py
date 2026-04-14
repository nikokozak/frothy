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
CONT_PROMPT = b".. "
BLINK_PROOF = os.path.join(ROOT_DIR, "tools", "frothy", "proof_m10_blink.frothy")
BOOT_PROOF = os.path.join(
    ROOT_DIR, "tools", "frothy", "proof_m10_boot_persist.frothy"
)
CELLS_PROOF = os.path.join(
    ROOT_DIR, "tools", "frothy", "proof_m10_cells_adc.frothy"
)
WORKSHOP_PROOF = os.path.join(
    ROOT_DIR, "tools", "frothy", "proof_m10_workshop_surface.frothy"
)
HOMEBREW_BIN = "/opt/homebrew/bin"


def idf_env() -> dict[str, str]:
    env = os.environ.copy()
    homebrew_bin = "/opt/homebrew/bin"
    current_path = env.get("PATH", "")
    if os.path.isdir(homebrew_bin):
        if current_path:
            env["PATH"] = f"{homebrew_bin}:{current_path}"
        else:
            env["PATH"] = homebrew_bin
    return env


def idf_shell_prefix() -> str:
    if os.path.isdir(HOMEBREW_BIN):
        return f'export PATH={shlex.quote(HOMEBREW_BIN)}:$PATH; '
    return ""


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_contains(text: str, needle: str) -> None:
    if needle not in text:
        fail(f"expected transcript to contain: {needle}")


def require_match(text: str, pattern: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        fail(f"expected transcript to match: {pattern}")


def require_not_contains(text: str, needle: str) -> None:
    if needle in text:
        fail(f"expected transcript not to contain: {needle}")


def require_count_at_least(text: str, needle: str, expected: int) -> None:
    actual = text.count(needle)
    if actual < expected:
        fail(f"expected at least {expected} occurrences of {needle!r}, got {actual}")


def require_sequence(text: str, needles: list[str]) -> None:
    index = 0
    for needle in needles:
        next_index = text.find(needle, index)
        if next_index < 0:
            fail(f"expected transcript sequence element: {needle}")
        index = next_index + len(needle)


def ensure_idf_available() -> None:
    command = (
        idf_shell_prefix()
        +
        '. "$HOME/.froth/sdk/esp-idf/export.sh" >/dev/null 2>&1 && '
        "command -v idf.py"
    )
    result = subprocess.run(
        ["/bin/bash", "-lc", command],
        cwd=TARGET_DIR,
        env=idf_env(),
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        fail(
            "ESP-IDF tools are unavailable; expected export.sh to make "
            f"`idf.py` visible.\n{result.stdout}{result.stderr}"
        )


class IdfMonitorSession:
    def __init__(self, port: str, flash: bool) -> None:
        self.master_fd, slave_fd = pty.openpty()
        action = "flash monitor" if flash else "monitor"
        command = (
            idf_shell_prefix()
            +
            '. "$HOME/.froth/sdk/esp-idf/export.sh" >/dev/null 2>&1 && '
            f"idf.py -p {shlex.quote(port)} {action}"
        )
        self.proc = subprocess.Popen(
            ["/bin/bash", "-lc", command],
            cwd=TARGET_DIR,
            env=idf_env(),
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
                    try:
                        os.killpg(self.proc.pid, signal.SIGKILL)
                        try:
                            self.proc.wait(timeout=3.0)
                        except subprocess.TimeoutExpired:
                            pass
                    except (PermissionError, ProcessLookupError):
                        self.proc.kill()
                        try:
                            self.proc.wait(timeout=3.0)
                        except subprocess.TimeoutExpired:
                            pass
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

    def read_until_any(self, needles: list[bytes], timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        chunk = bytearray()

        while not any(needle in chunk for needle in needles):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                rendered = ", ".join(repr(needle) for needle in needles)
                fail(f"timed out waiting for one of [{rendered}]\n{self.text()}")

            ready, _, _ = select.select([self.master_fd], [], [], remaining)
            if not ready:
                continue

            data = os.read(self.master_fd, 4096)
            if not data:
                rendered = ", ".join(repr(needle) for needle in needles)
                fail(f"monitor exited before one of [{rendered}]\n{self.text()}")

            chunk.extend(data)
            self.transcript.extend(data)

        return bytes(chunk)

    def send_line(self, text: str, timeout: float = 10.0) -> None:
        os.write(self.master_fd, text.encode("utf-8") + b"\n")
        self.read_until_any([PROMPT, CONT_PROMPT], timeout)

    def send_raw(self, data: bytes) -> None:
        os.write(self.master_fd, data)

    def wait_for_stable_prompt(self, timeout: float, quiet_period: float = 1.0) -> None:
        deadline = time.monotonic() + timeout
        self.read_until(PROMPT, timeout)
        while deadline - time.monotonic() > 0:
            remaining = deadline - time.monotonic()
            wait_for = quiet_period if quiet_period < remaining else remaining
            ready, _, _ = select.select([self.master_fd], [], [], wait_for)
            if not ready:
                return
            data = os.read(self.master_fd, 4096)
            if not data:
                fail(f"monitor exited before prompt stabilized\n{self.text()}")
            self.transcript.extend(data)
            if PROMPT in data:
                continue
            self.read_until(PROMPT, deadline - time.monotonic())
        fail(f"timed out waiting for stable prompt\n{self.text()}")

    def run_file(self, path: str, timeout: float = 10.0) -> None:
        with open(path, "r", encoding="utf-8") as handle:
            for raw_line in handle:
                line = raw_line.rstrip("\n")
                if not line.strip():
                    continue
                self.send_line(line, timeout=timeout)

    def text(self) -> str:
        return self.transcript.decode("utf-8", errors="replace").replace(
            "\r\n", "\n"
        )


def confirm_blink(assume_yes: bool) -> str:
    if assume_yes:
        return "blink confirmation: assumed yes via --assume-blink-confirmed"

    answer = input("Did LED_BUILTIN visibly blink three times? [y/N]: ").strip().lower()
    if answer not in ("y", "yes"):
        fail("blink confirmation was not acknowledged")
    return "blink confirmation: yes"


def run_phase_one(session: IdfMonitorSession, assume_yes: bool) -> str:
    session.run_file(BLINK_PROOF)
    transcript = session.text()
    require_contains(transcript, "Frothy shell")
    require_not_contains(transcript, "RTCWDT_RTC_RESET")
    require_match(
        transcript,
        r"blink\(LED_BUILTIN, pulses, period\)\r?\nnil\r?\nfrothy> ",
    )
    return confirm_blink(assume_yes)


def run_phase_two(session: IdfMonitorSession) -> None:
    session.send_line("note")
    session.send_line("wipe()")
    session.send_line("note")
    session.run_file(BOOT_PROOF)
    transcript = session.text()
    require_contains(transcript, "snapshot: found")
    require_match(transcript, r'note\r?\n"booted"\r?\nfrothy> ')
    require_contains(transcript, "eval error (4)")


def run_phase_three(session: IdfMonitorSession) -> None:
    session.read_until(b"boot: CTRL-C for safe boot", timeout=120.0)
    session.send_raw(b"\x03")
    session.wait_for_stable_prompt(timeout=120.0)
    session.send_line("note")
    session.send_line("1 + 1")
    transcript = session.text()
    require_contains(transcript, "snapshot: found")
    require_contains(transcript, "boot: CTRL-C for safe boot")
    require_contains(transcript, "boot: Safe Boot, skipped restore and boot.")
    require_contains(transcript, "eval error (4)")
    require_match(transcript, r"1 \+ 1\r?\n2\r?\nfrothy> ")


def run_phase_four(session: IdfMonitorSession) -> None:
    session.run_file(CELLS_PROOF)
    session.run_file(WORKSHOP_PROOF)
    session.send_line("wipe()")
    transcript = session.text()
    matches = re.findall(r"sample\.read\(\d\)\r?\n(\d+)\r?\nfrothy> ", transcript)
    if len(matches) < 4:
        fail("expected four ADC sample readbacks in the transcript")
    require_contains(transcript, "millis | base | native | non-persistable | foreign")
    require_contains(transcript, "adc.percent | base | code | persistable | user")
    require_count_at_least(transcript, "blink | base | code | persistable | user", 2)
    require_sequence(
        transcript,
        [
            '"millis.check"',
            "true",
        ],
    )
    require_sequence(
        transcript,
        [
            '"gpio.low.check"',
            "0",
        ],
    )
    require_sequence(
        transcript,
        [
            '"gpio.high.check"',
            "1",
        ],
    )
    require_sequence(
        transcript,
        [
            '"gpio.toggle.check"',
            "0",
        ],
    )
    require_sequence(
        transcript,
        [
            '"led.off.check"',
            "0",
            '"led.on.check"',
            "1",
            '"led.toggle.check"',
            "0",
        ],
    )
    require_match(transcript, r"adc\.percent\(A0\)\r?\n(\d+)\r?\nfrothy> ")
    percent_match = re.search(r"adc\.percent\(A0\)\r?\n(\d+)\r?\nfrothy> ", transcript)
    if percent_match is None:
        fail("expected adc.percent(A0) in transcript")
    percent_value = int(percent_match.group(1))
    if percent_value < 0 or percent_value > 100:
        fail(f"expected adc.percent(A0) in 0..100, got {percent_value}")
    require_contains(transcript, "anim[0]")
    require_contains(transcript, "anim[1]")
    require_contains(transcript, "anim[2]")
    require_match(transcript, r"anim\[0\]\r?\n41\r?\nfrothy> ")
    require_match(transcript, r"anim\[1\]\r?\n42\r?\nfrothy> ")
    require_match(transcript, r"anim\[2\]\r?\n43\r?\nfrothy> ")
    require_sequence(
        transcript,
        [
            '"overlay.blink.check"',
            "blink | overlay | code | persistable | user",
            "99",
            '"restored.blink.check"',
            "blink | base | code | persistable | user",
            "[gpio] pin 2 -> OUTPUT",
            "[gpio] pin 2 = LOW",
            "[gpio] pin 2 = HIGH",
            "[gpio] pin 2 = LOW",
            "nil",
        ],
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Flash and exercise the Frothy M10 hardware proof bundle."
    )
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbserial-0001")
    parser.add_argument(
        "--assume-blink-confirmed",
        action="store_true",
        help="Skip the interactive LED confirmation prompt.",
    )
    parser.add_argument(
        "--transcript-out",
        help="Optional file path to write the combined proof transcript to.",
    )
    args = parser.parse_args()

    for path in (BLINK_PROOF, BOOT_PROOF, CELLS_PROOF, WORKSHOP_PROOF):
        if not os.path.isfile(path):
            fail(f"missing proof file: {path}")

    ensure_idf_available()

    emitted = []

    first_session = IdfMonitorSession(args.port, flash=True)
    try:
        first_session.wait_for_stable_prompt(timeout=120.0)
        blink_note = run_phase_one(first_session, args.assume_blink_confirmed)
        first_session.run_file(BOOT_PROOF)
        emitted.append(first_session.text())
        emitted.append(blink_note + "\n")
    finally:
        first_session.close()

    second_session = IdfMonitorSession(args.port, flash=True)
    try:
        second_session.wait_for_stable_prompt(timeout=120.0)
        run_phase_two(second_session)
        emitted.append(second_session.text())
    finally:
        second_session.close()

    third_session = IdfMonitorSession(args.port, flash=True)
    try:
        run_phase_three(third_session)
        run_phase_four(third_session)
        emitted.append(third_session.text())
    finally:
        third_session.close()

    combined = "".join(emitted)
    print(combined, end="")
    if args.transcript_out:
        with open(args.transcript_out, "w", encoding="utf-8") as handle:
            handle.write(combined)

    return 0


if __name__ == "__main__":
    sys.exit(main())
