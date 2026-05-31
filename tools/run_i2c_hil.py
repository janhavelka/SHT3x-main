#!/usr/bin/env python3
"""Serial HIL runner for the SHT3x Arduino/ESP-IDF diagnostic CLIs."""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import json
import re
import subprocess
import sys
import time
from pathlib import Path

PY_SERIAL_INSTALL_HINT = "python -m pip install pyserial"
ANSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT_S = 8.0
DEFAULT_IDLE_S = 0.35

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"
RESULT_SERIAL_REVIEW = "SERIAL_OK_OR_REVIEW"
RESULT_REVIEW = "REVIEW_REQUIRED"
RESULT_OPERATOR = "OPERATOR_CHECK_REQUIRED"
RESULT_SKIPPED = "SKIPPED_UNSAFE"
RESULT_TIMEOUT = "TIMEOUT"
RESULT_DRY = "NOT_IMPLEMENTED"


@dataclasses.dataclass(frozen=True)
class CommandSpec:
    command: str
    purpose: str
    expected: tuple[str, ...] = ()
    expected_any: tuple[str, ...] = ()
    failures: tuple[str, ...] = ()
    completion: tuple[str, ...] = ()
    timeout_s: float = DEFAULT_TIMEOUT_S
    pre_delay_s: float = 0.0
    operator_check: bool = False
    destructive: bool = False
    requires_opt_in: str | None = None
    recovery_command: str | None = None
    send: bool = True
    notes: str = ""


DEFAULT_COMMAND_SEQUENCE: tuple[CommandSpec, ...] = (
    CommandSpec("version", "Record firmware/library version.", expected_any=("SHT3x library", "library_version=", "library_full=")),
    CommandSpec("help", "Capture CLI command surface.", expected_any=("SHT3x CLI Help", "Commands:"), completion=("selftest", "Commands:")),
    CommandSpec("scan", "I2C address ACK scan.", expected_any=("0x44", "0x45", "Scan complete", "scan:"), notes="ACK alone is not chip identity."),
    CommandSpec("probe", "Driver probe using SHT3x status-frame path.", expected_any=("Status: OK", "probe: OK"), failures=("DEVICE_NOT_FOUND", "I2C_NACK_ADDR", "I2C_TIMEOUT")),
    CommandSpec("settings", "Record configuration and state.", expected_any=("=== Config ===", "state=", "mode=")),
    CommandSpec("drv", "Record health and last-error state.", expected_any=("Driver Health", "state=", "online=")),
    CommandSpec("status", "Read parsed status bits without clearing them.", expected_any=("status: raw=0x", "raw=0x")),
    CommandSpec("status_raw", "Read raw status word.", expected_any=("Status raw:", "status=0x")),
    CommandSpec("single high", "Run high-repeatability no-stretch measurement.", expected_any=("Temp:", "temperature="), timeout_s=12.0, failures=("CRC_MISMATCH", "MEASUREMENT_NOT_READY")),
    CommandSpec("raw", "Read cached raw sample.", expected_any=("Raw:", "rawT=0x")),
    CommandSpec("comp", "Read cached compensated sample.", expected_any=("Comp:", "tempC_x100=")),
    CommandSpec("serial nostretch", "Read CRC-protected SHT3x serial/EIC value.", expected_any=("Serial:", "serial=0x"), failures=("CRC_MISMATCH", "I2C_TIMEOUT")),
    CommandSpec("heater status", "Read heater state without enabling heater.", expected_any=("Heater:", "heater=")),
    CommandSpec("alert show", "Read alert-limit configuration while idle.", expected_any=("HIGH_SET", "alert_HIGH_SET"), timeout_s=12.0),
    CommandSpec("status_restore", "Exercise readStatusWithModeRestore() and log sub-statuses.", expected=("status_restore:", "statusReadStatus", "statusValid=1"), expected_any=("restored=1", "restored=true"), timeout_s=12.0),
    CommandSpec("periodic start 1 high", "Start volatile 1 mps periodic acquisition.", expected_any=("periodic start: OK", "start_periodic: OK", "Status: OK"), recovery_command="periodic stop"),
    CommandSpec("periodic fetch", "Fetch one periodic sample.", expected_any=("Temp:", "temperature="), pre_delay_s=1.4, timeout_s=12.0),
    CommandSpec("periodic stop", "Stop periodic acquisition.", expected_any=("periodic stop: OK", "stop_periodic: OK", "Status: OK")),
    CommandSpec("drv", "Final health snapshot.", expected_any=("Driver Health", "state=", "online=")),
)

OPTIONAL_COMMANDS: tuple[CommandSpec, ...] = (
    CommandSpec("selftest", "Run CLI selftest.", expected_any=("Selftest result:", "selftest: pass="), destructive=True, requires_opt_in="--include-destructive", recovery_command="settings", timeout_s=20.0, notes="Arduino selftest performs softReset()."),
    CommandSpec("recover", "Manual recovery attempt.", expected_any=("recover: OK", "Status: OK"), destructive=True, requires_opt_in="--include-destructive", recovery_command="settings", timeout_s=20.0),
    CommandSpec("stress 10", "Short stress run.", expected_any=("Stress Summary", "stress: ok="), requires_opt_in="--include-soak", timeout_s=60.0),
    CommandSpec("ALERT threshold procedure", "Manual ALERT pin validation.", operator_check=True, send=False, requires_opt_in="--include-output-tests", notes="Requires GPIO or logic-analyzer evidence."),
    CommandSpec("fault/unplug/CRC-injection procedure", "Manual fault validation.", operator_check=True, send=False, requires_opt_in="--include-fault-tests", notes="Requires a safe jig or interposer."),
)

FAIL_PATTERNS = [
    re.compile(r"\bERR\b"),
    re.compile(r"\bFAIL(?:ED)?\b"),
    re.compile(r"\bCRC_MISMATCH\b"),
    re.compile(r"\bDEVICE_NOT_FOUND\b"),
    re.compile(r"\bI2C_NACK_(?:ADDR|DATA|READ)\b"),
    re.compile(r"\bI2C_TIMEOUT\b"),
    re.compile(r"\bI2C_BUS\b"),
    re.compile(r"\bCOMMAND_FAILED\b"),
    re.compile(r"\bWRITE_CRC_ERROR\b"),
    re.compile(r"Unknown command"),
]


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def git_value(*args: str) -> str:
    try:
        return subprocess.run(["git", *args], cwd=repo_root(), text=True, check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()
    except Exception:
        return ""


def git_state() -> dict[str, str]:
    return {
        "branch": git_value("branch", "--show-current") or "unknown",
        "commit": git_value("rev-parse", "HEAD") or "unknown",
        "status_short": git_value("status", "--short"),
    }


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def default_executable_commands() -> list[str]:
    return [step.command for step in DEFAULT_COMMAND_SEQUENCE if step.send]


def enabled(spec: CommandSpec, args: argparse.Namespace) -> bool:
    return spec.requires_opt_in is None or {
        "--include-destructive": args.include_destructive,
        "--include-soak": args.include_soak,
        "--include-fault-tests": args.include_fault_tests,
        "--include-output-tests": args.include_output_tests,
    }.get(spec.requires_opt_in, False)


def command_file(path: Path, timeout_s: float) -> list[CommandSpec]:
    out: list[CommandSpec] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(CommandSpec(line, "User-provided command.", timeout_s=timeout_s, notes="Review transcript; no built-in expectations."))
    return out


def with_timeout(spec: CommandSpec, timeout_s: float) -> CommandSpec:
    if timeout_s == DEFAULT_TIMEOUT_S:
        return spec
    return dataclasses.replace(spec, timeout_s=max(spec.timeout_s, timeout_s))


def build_plan(args: argparse.Namespace) -> tuple[list[CommandSpec], list[CommandSpec]]:
    if args.commands:
        return command_file(Path(args.commands), args.timeout), []
    executable = [with_timeout(spec, args.timeout) for spec in DEFAULT_COMMAND_SEQUENCE]
    skipped: list[CommandSpec] = []
    for spec in OPTIONAL_COMMANDS:
        spec = with_timeout(spec, args.timeout)
        if enabled(spec, args) and spec.send:
            executable.append(spec)
        else:
            skipped.append(spec)
    return executable, skipped


def make_log_dir(base: Path) -> Path:
    base.mkdir(parents=True, exist_ok=True)
    stamp = timestamp()
    for idx in range(100):
        candidate = base / (f"i2c_{stamp}" if idx == 0 else f"i2c_{stamp}_{idx:02d}")
        try:
            candidate.mkdir()
            return candidate
        except FileExistsError:
            continue
    raise RuntimeError(f"cannot create unique log directory under {base}")


def has_failure(text: str, spec: CommandSpec) -> bool:
    plain = strip_ansi(text)
    return any(token in plain for token in spec.failures) or any(pattern.search(plain) for pattern in FAIL_PATTERNS)


def expected(text: str, spec: CommandSpec) -> bool:
    plain = strip_ansi(text)
    all_ok = all(token in plain for token in spec.expected)
    any_ok = True if not spec.expected_any else any(token in plain for token in spec.expected_any)
    return (all_ok and any_ok) if (spec.expected or spec.expected_any) else bool(plain.strip())


def classify(text: str, spec: CommandSpec, timed_out: bool) -> str:
    if spec.operator_check:
        return RESULT_OPERATOR
    if timed_out:
        return RESULT_TIMEOUT
    if has_failure(text, spec):
        return RESULT_FAIL
    if expected(text, spec):
        return RESULT_PASS
    return RESULT_SERIAL_REVIEW if text.strip() else RESULT_REVIEW


def read_available(ser: object) -> str:
    data = ser.read(4096)
    return data.decode("utf-8", errors="replace") if data else ""


def drain_initial_output(ser: object, idle_s: float, max_s: float = 2.0) -> str:
    start = time.monotonic()
    last_rx = start
    parts: list[str] = []
    while time.monotonic() - start < max_s:
        chunk = read_available(ser)
        now = time.monotonic()
        if chunk:
            parts.append(chunk)
            last_rx = now
        elif parts and now - last_rx >= idle_s:
            break
        time.sleep(0.02)
    return "".join(parts)


def run_serial(ser: object, spec: CommandSpec, idle_s: float) -> dict[str, object]:
    if spec.pre_delay_s:
        time.sleep(spec.pre_delay_s)
    start = time.monotonic()
    deadline = start + spec.timeout_s
    tokens = spec.completion or spec.expected or spec.expected_any
    parts: list[str] = []
    last_rx = start
    reason = "timeout"
    ser.write((spec.command + "\n").encode("utf-8"))
    ser.flush()
    while time.monotonic() < deadline:
        chunk = read_available(ser)
        now = time.monotonic()
        if chunk:
            parts.append(chunk)
            last_rx = now
        plain = strip_ansi("".join(parts))
        token_seen = bool(tokens) and any(token in plain for token in tokens)
        idle = (now - last_rx) >= idle_s
        prompt_seen = plain.rstrip().endswith(">")
        if (token_seen or (not tokens and prompt_seen) or (not tokens and plain.strip())) and idle:
            reason = "completion-token+idle" if token_seen else "serial-idle"
            break
        time.sleep(0.02)
    output = "".join(parts)
    timed_out = reason == "timeout"
    return {
        "command": spec.command,
        "purpose": spec.purpose,
        "result": classify(output, spec, timed_out),
        "completion_reason": reason,
        "elapsed_s": round(time.monotonic() - start, 3),
        "output": output,
        "notes": spec.notes,
        "operator_check": spec.operator_check,
        "operator_result": RESULT_OPERATOR if spec.operator_check else "N/A",
        "destructive": spec.destructive,
        "requires_opt_in": spec.requires_opt_in,
        "recovery_command": spec.recovery_command,
    }


def run_dry(spec: CommandSpec) -> dict[str, object]:
    return {
        "command": spec.command,
        "purpose": spec.purpose,
        "result": RESULT_OPERATOR if spec.operator_check else RESULT_DRY,
        "completion_reason": "dry-run",
        "elapsed_s": 0.0,
        "output": "",
        "notes": spec.notes,
        "operator_check": spec.operator_check,
        "operator_result": RESULT_OPERATOR if spec.operator_check else "N/A",
        "destructive": spec.destructive,
        "requires_opt_in": spec.requires_opt_in,
        "recovery_command": spec.recovery_command,
    }


def verdict(results: list[dict[str, object]], dry_run: bool) -> str:
    if dry_run:
        return "INCOMPLETE"
    values = {str(row["result"]) for row in results}
    if values & {RESULT_FAIL, RESULT_TIMEOUT}:
        return "FAIL"
    if values & {RESULT_SERIAL_REVIEW, RESULT_REVIEW, RESULT_OPERATOR}:
        return "OPERATOR_REVIEW_REQUIRED"
    return "PASS" if values == {RESULT_PASS} else "INCOMPLETE"


def write_transcript(path: Path, args: argparse.Namespace, results: list[dict[str, object]], initial_output: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("SHT3x serial HIL transcript\n")
        fh.write(f"timestamp_utc={timestamp()}\nport={args.port or '<dry-run>'}\nbaud={args.baud}\ndry_run={int(args.dry_run)}\n\n")
        fh.write("===== INITIAL SERIAL OUTPUT =====\n")
        fh.write(initial_output)
        if initial_output and not initial_output.endswith("\n"):
            fh.write("\n")
        fh.write("===== END INITIAL SERIAL OUTPUT =====\n\n")
        for row in results:
            fh.write(f"===== COMMAND: {row['command']} =====\n")
            fh.write(str(row.get("output", "")))
            if row.get("output") and not str(row.get("output")).endswith("\n"):
                fh.write("\n")
            fh.write(f"===== RESULT: {row['result']} ({row['completion_reason']}) =====\n\n")


def md_table(results: list[dict[str, object]]) -> str:
    rows = ["| Command | Purpose | Serial result | Operator result | Completion | Elapsed s | Notes |",
            "| --- | --- | --- | --- | --- | ---: | --- |"]
    for row in results:
        rows.append(f"| `{row['command']}` | {str(row['purpose']).replace('|', '\\|')} | `{row['result']}` | `{row.get('operator_result', 'N/A')}` | {row['completion_reason']} | {row['elapsed_s']} | {str(row.get('notes', '')).replace('|', '\\|')} |")
    return "\n".join(rows)


def write_checklist(path: Path, skipped: list[CommandSpec]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# SHT3x HIL Operator Checklist\n\n")
        fh.write("These checks are not proven by the default automated serial smoke run.\n\n")
        fh.write("| Item | Required opt-in | Evidence needed |\n| --- | --- | --- |\n")
        for spec in skipped:
            fh.write(f"| `{spec.command}` | `{spec.requires_opt_in or 'manual'}` | {spec.notes.replace('|', '\\|')} |\n")


def write_summary_md(path: Path, args: argparse.Namespace, log_dir: Path, results: list[dict[str, object]], skipped: list[CommandSpec], final: str, state: dict[str, str]) -> None:
    dirty = "dirty" if state.get("status_short") else "clean"
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# SHT3x I2C HIL Summary\n\n")
        fh.write(f"Date/time UTC: {timestamp()}\n\nBranch: `{state['branch']}`\n\nCommit: `{state['commit']}`\n\n")
        fh.write(f"Worktree state: `{dirty}`\n\nSerial port: `{args.port or '<dry-run>'}`\n\nBaud rate: `{args.baud}`\n\nDry run: `{args.dry_run}`\n\n")
        fh.write("Firmware/framework: `see version/help transcript if reported`\n\nI2C address: `configured by target firmware; expected SHT3x default is 0x44 unless ADDR high uses 0x45`\n\nDevice/module info: `operator target template and transcript`\n\n")
        fh.write("Initial serial output is captured in `serial_transcript.txt` before the first command.\n\nRunner boundary: host controls the repository ESP32 diagnostic CLI over serial. ACK alone is not chip identity.\n\n")
        fh.write("## Configured Command Sequence\n\n")
        for row in results:
            fh.write(f"- `{row['command']}`\n")
        fh.write("\n")
        fh.write("## Per-Command Results\n\n")
        fh.write(md_table(results))
        fh.write("\n\n## Skipped / Manual Checks\n\n")
        for spec in skipped:
            fh.write(f"- `{spec.command}`: {spec.notes} ({spec.requires_opt_in or 'manual'})\n")
        fh.write(f"\n## Final Verdict\n\n`{final}`\n\n")
        fh.write("A PASS verdict is limited to the automated serial command sequence. It does not prove humidity accuracy, physical ALERT pin behavior, fault injection, long soak stability, or production readiness unless those groups were run with evidence.\n\n")
        fh.write(f"## Artifacts\n\n- `{log_dir / 'serial_transcript.txt'}`\n- `{log_dir / 'summary.json'}`\n- `{log_dir / 'operator_checklist.md'}`\n")


def write_summary_json(path: Path, args: argparse.Namespace, log_dir: Path, results: list[dict[str, object]], skipped: list[CommandSpec], final: str, state: dict[str, str], initial_output: str) -> None:
    payload = {
        "timestamp_utc": timestamp(),
        "branch": state["branch"],
        "commit": state["commit"],
        "worktree_status_short": state["status_short"],
        "port": args.port,
        "baud": args.baud,
        "dry_run": args.dry_run,
        "initial_serial_output_present": bool(initial_output),
        "runner_boundary": "host serial control of ESP32 diagnostic CLI",
        "identity_boundary": "ACK alone is not chip identity",
        "commands": results,
        "skipped": [dataclasses.asdict(spec) for spec in skipped],
        "verdict": final,
        "artifacts": {
            "log_dir": str(log_dir),
            "serial_transcript": str(log_dir / "serial_transcript.txt"),
            "summary_md": str(log_dir / "summary.md"),
            "summary_json": str(path),
            "operator_checklist": str(log_dir / "operator_checklist.md"),
        },
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def open_serial(args: argparse.Namespace) -> object:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        print("pyserial is required for serial HIL.", file=sys.stderr)
        print(f"Install with: {PY_SERIAL_INSTALL_HINT}", file=sys.stderr)
        raise SystemExit(2) from exc
    ser = serial.Serial(port=args.port, baudrate=args.baud, timeout=0.05, write_timeout=2)
    ser.dtr = False
    ser.rts = False
    time.sleep(1.0)
    return ser


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port, for example COM7 or /dev/ttyACM0.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--out", default="hil_logs")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--idle", type=float, default=DEFAULT_IDLE_S)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--commands", help="Optional command file, one CLI command per line.")
    parser.add_argument("--include-destructive", action="store_true")
    parser.add_argument("--include-soak", action="store_true")
    parser.add_argument("--include-fault-tests", action="store_true")
    parser.add_argument("--include-output-tests", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.dry_run and not args.port:
        print("--port is required unless --dry-run is used", file=sys.stderr)
        return 2
    log_dir = make_log_dir(Path(args.out))
    specs, skipped = build_plan(args)
    results: list[dict[str, object]] = []
    initial_output = ""
    if args.dry_run:
        results = [run_dry(spec) for spec in specs]
    else:
        ser = open_serial(args)
        try:
            initial_output = drain_initial_output(ser, args.idle)
            for spec in specs:
                results.append(run_serial(ser, spec, args.idle))
        finally:
            ser.close()
    state = git_state()
    final = verdict(results, args.dry_run)
    write_transcript(log_dir / "serial_transcript.txt", args, results, initial_output)
    write_checklist(log_dir / "operator_checklist.md", skipped)
    write_summary_md(log_dir / "summary.md", args, log_dir, results, skipped, final, state)
    write_summary_json(log_dir / "summary.json", args, log_dir, results, skipped, final, state, initial_output)
    print(f"HIL log directory: {log_dir}")
    print(f"Final verdict: {final}")
    print(f"Operator checklist: {log_dir / 'operator_checklist.md'}")
    checklist = log_dir / "operator_checklist.md"
    if checklist.exists():
        print("\n" + checklist.read_text(encoding="utf-8"))
    if args.dry_run:
        print("Dry run only. No physical HIL validation was performed.")
    return 1 if final == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
