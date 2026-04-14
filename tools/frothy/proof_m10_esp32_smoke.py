#!/usr/bin/env python3
import argparse
import os
import pty
import re
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time


ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TARGET_DIR = os.path.join(ROOT_DIR, "targets", "esp-idf")
CLI_BIN = os.path.join(ROOT_DIR, "tools", "cli", "froth-cli")
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
WORKSHOP_STARTER_CHECKS_PROOF = os.path.join(
    ROOT_DIR, "tools", "frothy", "proof_m10_workshop_starter_checks.frothy"
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
    start = session.text()
    session.run_file(BLINK_PROOF)
    transcript = session.text()
    segment = transcript[len(start) :]
    require_contains(transcript, "Frothy shell")
    require_not_contains(transcript, "RTCWDT_RTC_RESET")
    require_match(
        segment,
        r"blink: LED_BUILTIN, pulses, period\r?\nnil\r?\nfrothy> ",
    )
    require_not_contains(segment, "eval error (")
    require_not_contains(segment, "parse error (")
    return confirm_blink(assume_yes)


def run_phase_two(session: IdfMonitorSession) -> None:
    boot_transcript = session.text()
    session.send_line("note")
    session.send_line("dangerous.wipe")
    session.send_line("note")
    session.run_file(BOOT_PROOF)
    transcript = session.text()
    segment = transcript[len(boot_transcript) :]
    require_contains(boot_transcript, "snapshot: found")
    require_sequence(
        segment,
        [
            "note",
            '"booted"',
            "note",
            "eval error (4)",
        ],
    )
    require_not_contains(segment, "parse error (")


def run_phase_three(session: IdfMonitorSession) -> None:
    start = len(session.text())
    session.read_until(b"boot: CTRL-C for safe boot", timeout=120.0)
    session.send_raw(b"\x03")
    session.wait_for_stable_prompt(timeout=120.0)
    session.send_line("note")
    session.send_line("1 + 1")
    transcript = session.text()
    segment = transcript[start:]
    require_contains(transcript, "snapshot: found")
    require_contains(segment, "boot: CTRL-C for safe boot")
    require_contains(segment, "boot: Safe Boot, skipped restore and boot.")
    require_contains(segment, "eval error (4)")
    require_match(segment, r"1 \+ 1\r?\n2\r?\nfrothy> ")
    require_not_contains(segment, "parse error (")


def build_workshop_starter_proof() -> tuple[str, str]:
    temp_dir = tempfile.mkdtemp(prefix="frothy-m10-workshop-")
    starter_dir = os.path.join(temp_dir, "workshop-starter")
    starter_proof = os.path.join(temp_dir, "workshop-starter-proof.frothy")
    scaffold = subprocess.run(
        [CLI_BIN, "new", "--target", "esp32-devkit-v1", starter_dir],
        cwd=ROOT_DIR,
        env=idf_env(),
        capture_output=True,
        text=True,
        check=False,
    )
    if scaffold.returncode != 0:
        fail(f"failed to scaffold workshop starter\n{scaffold.stdout}{scaffold.stderr}")

    resolved = subprocess.run(
        [CLI_BIN, "tooling", "resolve-source", os.path.join(starter_dir, "src", "main.froth")],
        cwd=ROOT_DIR,
        env=idf_env(),
        capture_output=True,
        text=True,
        check=False,
    )
    if resolved.returncode != 0:
        fail(
            "failed to resolve workshop starter source\n"
            f"{resolved.stdout}{resolved.stderr}"
        )
    if "warning:" in resolved.stderr:
        fail(f"workshop starter resolve emitted warnings\n{resolved.stderr}")

    with open(starter_proof, "w", encoding="utf-8") as handle:
        handle.write(resolved.stdout)
        if resolved.stdout and not resolved.stdout.endswith("\n"):
            handle.write("\n")
        with open(WORKSHOP_STARTER_CHECKS_PROOF, "r", encoding="utf-8") as tail:
            handle.write(tail.read())

    return temp_dir, starter_proof


def run_phase_four(session: IdfMonitorSession) -> None:
    phase_start = session.text()
    session.run_file(CELLS_PROOF)
    cells_transcript = session.text()[len(phase_start) :]
    session.run_file(WORKSHOP_PROOF)
    workshop_transcript = session.text()[len(phase_start) + len(cells_transcript) :]
    session.send_line("dangerous.wipe")
    temp_dir, starter_proof = build_workshop_starter_proof()
    starter_start = len(session.text())
    try:
        session.run_file(starter_proof)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)
    starter_transcript = session.text()[starter_start:]
    matches = re.findall(r"sample\.read: \d\r?\n(\d+)\r?\nfrothy> ", cells_transcript)
    if len(matches) < 4:
        fail("expected four ADC sample readbacks in the transcript")
    require_contains(workshop_transcript, "info @millis")
    require_contains(workshop_transcript, "help: Return wrapped monotonic uptime in milliseconds.")
    require_contains(workshop_transcript, "info @blink")
    require_contains(workshop_transcript, "info @adc.percent")
    require_contains(workshop_transcript, "owner: board ffi")
    require_count_at_least(workshop_transcript, "owner: base image", 2)
    require_match(
        workshop_transcript,
        r'"millis\.check"\r?\n[\s\S]*?after > start\r?\ntrue\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"gpio\.low\.check"\r?\n[\s\S]*?gpio\.read: LED_BUILTIN\r?\n0\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"gpio\.high\.check"\r?\n[\s\S]*?gpio\.read: LED_BUILTIN\r?\n1\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"gpio\.toggle\.check"\r?\n[\s\S]*?gpio\.read: LED_BUILTIN\r?\n[01]\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"led\.off\.check"\r?\n[\s\S]*?gpio\.read: LED_BUILTIN\r?\n0\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"led\.on\.check"\r?\n[\s\S]*?gpio\.read: LED_BUILTIN\r?\n1\r?\nfrothy> ',
    )
    toggle_match = re.search(
        r'"led\.toggle\.check"\r?\n.*?gpio\.read: LED_BUILTIN\r?\n(\d+)\r?\nfrothy> ',
        workshop_transcript,
        re.DOTALL,
    )
    if toggle_match is None:
        fail("expected led.toggle.check readback in transcript")
    toggle_value = int(toggle_match.group(1))
    if toggle_value < 0 or toggle_value > 1:
        fail(f"expected led.toggle.check readback in 0..1, got {toggle_value}")
    require_match(
        workshop_transcript,
        r'"led\.blink\.check"\r?\n[\s\S]*?led\.blink: 1, 1\r?\nnil\r?\nfrothy> ',
    )
    percent_match = re.search(
        r'"adc\.percent\.check"\r?\n.*?adc\.percent: A0\r?\n(\d+)\r?\nfrothy> ',
        workshop_transcript,
        re.DOTALL,
    )
    if percent_match is None:
        fail("expected adc.percent: A0 in transcript")
    percent_value = int(percent_match.group(1))
    if percent_value < 0 or percent_value > 100:
        fail(f"expected adc.percent: A0 in 0..100, got {percent_value}")
    require_match(
        workshop_transcript,
        r'"anim\.check"\r?\n[\s\S]*?anim\[0\]\r?\n41\r?\nfrothy> [\s\S]*?anim\[1\]\r?\n42\r?\nfrothy> [\s\S]*?anim\[2\]\r?\n43\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"overlay\.blink\.check"\r?\n[\s\S]*?slot: overlay\r?\n[\s\S]*?owner: overlay image\r?\n[\s\S]*?blink: LED_BUILTIN, 1, 1\r?\n99\r?\nfrothy> ',
    )
    require_match(
        workshop_transcript,
        r'"restored\.blink\.check"\r?\n[\s\S]*?slot: base\r?\n[\s\S]*?owner: base image\r?\n[\s\S]*?blink: LED_BUILTIN, 1, 1\r?\nnil\r?\nfrothy> ',
    )
    require_not_contains(workshop_transcript, "eval error (")
    require_not_contains(workshop_transcript, "parse error (")
    require_sequence(
        starter_transcript,
        [
            '"boot.check"',
            '"Workshop starter ready"',
            'player[0]',
            '0',
            'player[1]',
            '0',
            'score',
            '0',
        ],
    )
    require_sequence(
        starter_transcript,
        [
            '"lesson.ready.check"',
            '"Workshop starter ready"',
        ],
    )
    require_match(
        starter_transcript,
        r'"lesson\.blink\.check"\r?\n[\s\S]*?led\.blink: 1, 1\r?\nnil\r?\nfrothy> ',
    )
    require_sequence(
        starter_transcript,
        [
            '"lesson.animate.check"',
            'anim[0]',
            '7',
            'anim[1]',
            '8',
            'anim[2]',
            '9',
        ],
    )
    require_sequence(
        starter_transcript,
        [
            '"game.capture.check"',
            'player[0]',
            '1',
            'player[1]',
            '1',
            'score',
        ],
    )
    score_match = re.search(
        r'"game\.capture\.check".*?score\r?\n(\d+)\r?\nfrothy> ',
        starter_transcript,
        re.MULTILINE | re.DOTALL,
    )
    if score_match is None:
        fail("expected score readback after game.capture")
    score_value = int(score_match.group(1))
    if score_value < 0 or score_value > 100:
        fail(f"expected game.capture score in 0..100, got {score_value}")
    require_sequence(
        starter_transcript,
        [
            '"game.restore.check"',
            'player[0]',
            '1',
            'player[1]',
            '1',
            'score',
            str(score_value),
        ],
    )
    require_not_contains(starter_transcript, "eval error (")
    require_not_contains(starter_transcript, "parse error (")


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

    for path in (
        BLINK_PROOF,
        BOOT_PROOF,
        CELLS_PROOF,
        WORKSHOP_PROOF,
        WORKSHOP_STARTER_CHECKS_PROOF,
    ):
        if not os.path.isfile(path):
            fail(f"missing proof file: {path}")
    if not os.access(CLI_BIN, os.X_OK):
        fail(f"missing froth CLI binary: {CLI_BIN}")

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
