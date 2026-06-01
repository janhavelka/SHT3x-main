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
from typing import Any

PY_SERIAL_INSTALL_HINT = "python -m pip install pyserial"
ANSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT_S = 8.0
DEFAULT_IDLE_S = 0.35
CLAIM_BOUNDARY = (
    "PASS is limited to the selected automated serial command groups. It does "
    "not prove humidity accuracy, physical ALERT pin behavior, fault injection, "
    "long-soak stability, or production readiness without the matching fixture "
    "or operator evidence."
)

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"
RESULT_SKIP = "SKIP"
RESULT_OPERATOR = "OPERATOR_REVIEW_REQUIRED"

VERDICT_PASS = "PASS"
VERDICT_FAIL = "FAIL"
VERDICT_OPERATOR = "OPERATOR_REVIEW_REQUIRED"
VERDICT_INCOMPLETE = "INCOMPLETE"

SKIP_DRY_RUN = "SKIP_DRY_RUN"
SKIP_NOT_SELECTED = "NOT_RUN"
SKIP_UNSUPPORTED = "SKIP_UNSUPPORTED"
SKIP_REQUIRES_FIXTURE = "SKIP_REQUIRES_FIXTURE"

TEMP_MIN_C = -40.0
TEMP_MAX_C = 125.0
HUMIDITY_MIN_PCT = 0.0
HUMIDITY_MAX_PCT = 100.0


@dataclasses.dataclass(frozen=True)
class CommandSpec:
    command: str
    purpose: str
    group: str = "default"
    expected: tuple[str, ...] = ()
    expected_any: tuple[str, ...] = ()
    failures: tuple[str, ...] = ()
    completion: tuple[str, ...] = ()
    validators: tuple[str, ...] = ()
    timeout_s: float = DEFAULT_TIMEOUT_S
    pre_delay_s: float = 0.0
    recovery_command: str | None = None
    requires_opt_in: str | None = None
    destructive: bool = False
    send: bool = True
    operator_check: bool = False
    fixture_required: bool = False
    review_only: bool = False
    unsupported_ok: bool = False
    expected_encoded: str | None = None
    expected_temperature_c: float | None = None
    expected_humidity_pct: float | None = None
    tolerance: float = 2.0
    notes: str = ""


def version_step() -> CommandSpec:
    return CommandSpec(
        "version",
        "Record firmware and library version.",
        expected_any=("SHT3x library version:", "library_version="),
        validators=("version",),
    )


DEFAULT_COMMAND_SEQUENCE: tuple[CommandSpec, ...] = (
    version_step(),
    CommandSpec("help", "Capture CLI command surface.", expected_any=("SHT3x CLI Help", "Commands:"), completion=("selftest", "Commands:")),
    CommandSpec("scan", "I2C address ACK scan.", expected_any=("0x44", "0x45", "Scan complete", "scan:", "found 0x"), validators=("expected_address",), notes="ACK alone is not chip identity."),
    CommandSpec("probe", "Driver probe using SHT3x status-frame path.", expected_any=("Status: OK", "probe: OK", "probe: OK code=0"), failures=("DEVICE_NOT_FOUND", "I2C_NACK_ADDR", "I2C_TIMEOUT")),
    CommandSpec("settings", "Record configuration and state.", expected_any=("=== Config ===", "state=", "mode="), validators=("driver_ready",)),
    CommandSpec("drv", "Record health and last-error state.", expected_any=("Driver Health", "state=", "online="), validators=("driver_ready",)),
    CommandSpec("status", "Read parsed status bits without clearing them.", expected_any=("status: raw=0x", "raw=0x"), validators=("status_word",)),
    CommandSpec("status_raw", "Read raw status word.", expected_any=("Status raw:", "status=0x"), validators=("status_word",)),
    CommandSpec("single low", "Run low-repeatability no-stretch measurement.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), timeout_s=12.0, failures=("CRC_MISMATCH", "MEASUREMENT_NOT_READY")),
    CommandSpec("raw", "Read cached raw sample.", expected_any=("Raw:", "rawT=0x"), validators=("raw_sample",)),
    CommandSpec("comp", "Read cached compensated sample.", expected_any=("Comp:", "tempC_x100="), validators=("comp_sample",)),
    CommandSpec("single medium", "Run medium-repeatability no-stretch measurement.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), timeout_s=12.0, failures=("CRC_MISMATCH", "MEASUREMENT_NOT_READY")),
    CommandSpec("raw", "Read cached raw sample.", expected_any=("Raw:", "rawT=0x"), validators=("raw_sample",)),
    CommandSpec("comp", "Read cached compensated sample.", expected_any=("Comp:", "tempC_x100="), validators=("comp_sample",)),
    CommandSpec("single high", "Run high-repeatability no-stretch measurement.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), timeout_s=12.0, failures=("CRC_MISMATCH", "MEASUREMENT_NOT_READY")),
    CommandSpec("raw", "Read cached raw sample.", expected_any=("Raw:", "rawT=0x"), validators=("raw_sample",)),
    CommandSpec("comp", "Read cached compensated sample.", expected_any=("Comp:", "tempC_x100="), validators=("comp_sample",)),
    CommandSpec("serial nostretch", "Read CRC-protected SHT3x serial/EIC value.", expected_any=("Serial:", "serial=0x"), validators=("serial",), failures=("CRC_MISMATCH", "I2C_TIMEOUT")),
    CommandSpec("heater status", "Read heater state without enabling heater.", expected_any=("Heater:", "heater="), validators=("heater_off",)),
    CommandSpec("alert show", "Read alert-limit configuration while idle.", expected_any=("HIGH_SET", "alert_HIGH_SET", "raw=0x"), validators=("alert_read",), timeout_s=12.0),
    CommandSpec("alert encode 60 80", "Encode vendor alert high-set vector.", expected_any=("Alert encoded:", "encoded=0x"), validators=("alert_encoded",), expected_encoded="0xCD33"),
    CommandSpec("alert decode 0xCD33", "Decode vendor alert high-set vector.", expected_any=("Alert decoded:", "temperature="), validators=("alert_decoded",), expected_temperature_c=60.0, expected_humidity_pct=80.0),
    CommandSpec("alert encode 58 79", "Encode vendor alert high-clear vector.", expected_any=("Alert encoded:", "encoded=0x"), validators=("alert_encoded",), expected_encoded="0xC92D"),
    CommandSpec("alert decode 0xC92D", "Decode vendor alert high-clear vector.", expected_any=("Alert decoded:", "temperature="), validators=("alert_decoded",), expected_temperature_c=58.0, expected_humidity_pct=79.0),
    CommandSpec("alert encode -9 22", "Encode vendor alert low-clear vector.", expected_any=("Alert encoded:", "encoded=0x"), validators=("alert_encoded",), expected_encoded="0x3869"),
    CommandSpec("alert decode 0x3869", "Decode vendor alert low-clear vector.", expected_any=("Alert decoded:", "temperature="), validators=("alert_decoded",), expected_temperature_c=-9.0, expected_humidity_pct=22.0),
    CommandSpec("alert encode -10 20", "Encode vendor alert low-set vector.", expected_any=("Alert encoded:", "encoded=0x"), validators=("alert_encoded",), expected_encoded="0x3466"),
    CommandSpec("alert decode 0x3466", "Decode vendor alert low-set vector.", expected_any=("Alert decoded:", "temperature="), validators=("alert_decoded",), expected_temperature_c=-10.0, expected_humidity_pct=20.0),
    CommandSpec("status_restore", "Exercise readStatusWithModeRestore() and log sub-statuses.", expected=("status_restore:", "statusReadStatus", "statusValid=1"), expected_any=("restored=1", "restored=true"), validators=("status_restore",), timeout_s=12.0),
    CommandSpec("periodic start 0.5 high", "Start volatile 0.5 mps periodic acquisition.", expected_any=("periodic start: OK", "start_periodic: OK", "Status: OK", "periodic start: OK code=0"), recovery_command="periodic stop"),
    CommandSpec("periodic fetch", "Fetch one 0.5 mps periodic sample.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), pre_delay_s=2.6, timeout_s=14.0),
    CommandSpec("periodic stop", "Stop periodic acquisition.", expected_any=("periodic stop: OK", "stop_periodic: OK", "Status: OK", "periodic stop: OK code=0")),
    CommandSpec("periodic start 1 high", "Start volatile 1 mps periodic acquisition.", expected_any=("periodic start: OK", "start_periodic: OK", "Status: OK", "periodic start: OK code=0"), recovery_command="periodic stop"),
    CommandSpec("periodic fetch", "Fetch one 1 mps periodic sample.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), pre_delay_s=1.4, timeout_s=12.0),
    CommandSpec("periodic stop", "Stop periodic acquisition.", expected_any=("periodic stop: OK", "stop_periodic: OK", "Status: OK", "periodic stop: OK code=0")),
    CommandSpec("periodic start 2 medium", "Start volatile 2 mps periodic acquisition.", expected_any=("periodic start: OK", "start_periodic: OK", "Status: OK", "periodic start: OK code=0"), recovery_command="periodic stop"),
    CommandSpec("periodic fetch", "Fetch one 2 mps periodic sample.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), pre_delay_s=0.8, timeout_s=12.0),
    CommandSpec("periodic stop", "Stop periodic acquisition.", expected_any=("periodic stop: OK", "stop_periodic: OK", "Status: OK", "periodic stop: OK code=0")),
    CommandSpec("art start", "Start ART mode.", expected_any=("art start: OK", "start_art: OK", "Status: OK", "art start: OK code=0"), recovery_command="art stop", unsupported_ok=True),
    CommandSpec("art fetch", "Fetch one ART sample.", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), pre_delay_s=1.0, timeout_s=12.0, unsupported_ok=True),
    CommandSpec("art stop", "Stop ART mode.", expected_any=("art stop: OK", "stop_periodic: OK", "Status: OK", "art stop: OK code=0"), unsupported_ok=True),
    CommandSpec("drv", "Final health snapshot.", expected_any=("Driver Health", "state=", "online="), validators=("driver_ready", "zero_failures")),
)


def destructive_commands(include_bus_wide_reset: bool) -> list[CommandSpec]:
    specs = [
        CommandSpec("selftest", "Run CLI selftest.", group="destructive", expected_any=("Selftest result:", "selftest: pass="), requires_opt_in="--include-destructive", destructive=True, recovery_command="settings", timeout_s=25.0, notes="Arduino selftest performs softReset()."),
        CommandSpec("recover", "Manual recovery attempt.", group="destructive", expected_any=("recover: OK", "Status: OK"), requires_opt_in="--include-destructive", destructive=True, recovery_command="settings", timeout_s=20.0),
        CommandSpec("status", "Read status before clear_status.", group="destructive", expected_any=("status: raw=0x", "raw=0x"), validators=("status_word",), requires_opt_in="--include-destructive", destructive=True),
        CommandSpec("clear_status", "Clear status flags.", group="destructive", expected_any=("Status: OK", "clearstatus: OK", "clearstatus: OK code=0"), requires_opt_in="--include-destructive", destructive=True),
        CommandSpec("status", "Read status after clear_status.", group="destructive", expected_any=("status: raw=0x", "raw=0x"), validators=("status_word",), requires_opt_in="--include-destructive", destructive=True),
        CommandSpec("reset", "Soft reset sensor.", group="destructive", expected_any=("Status: OK", "reset: OK", "reset: OK code=0"), requires_opt_in="--include-destructive", destructive=True, recovery_command="settings", timeout_s=20.0),
        CommandSpec("restore", "Reset sensor and restore cached settings.", group="destructive", expected_any=("Status: OK", "restore: OK", "restore: OK code=0"), requires_opt_in="--include-destructive", destructive=True, recovery_command="settings", timeout_s=20.0),
        CommandSpec("iface_reset", "Run application-provided interface reset callback.", group="destructive", expected_any=("Status: OK", "iface_reset: OK", "iface_reset: OK code=0"), requires_opt_in="--include-destructive", destructive=True, recovery_command="settings", timeout_s=20.0, unsupported_ok=True),
    ]
    if include_bus_wide_reset:
        specs.append(CommandSpec("greset", "General-call reset; bus-wide and unsafe on shared buses.", group="bus-wide-reset", expected_any=("Status: OK", "greset: OK", "greset: OK code=0"), requires_opt_in="--include-bus-wide-reset", destructive=True, recovery_command="settings", timeout_s=20.0, notes="Affects all devices that support general-call reset."))
    return specs


def soak_commands(count: int) -> list[CommandSpec]:
    count = max(1, count)
    mix_count = max(1, count // 2)
    return [
        CommandSpec("stress 10", "Short stress run.", group="soak", expected_any=("Stress Summary", "stress: ok=", "stress summary"), requires_opt_in="--include-soak", timeout_s=90.0),
        CommandSpec(f"stress {count}", "Configured bounded stress run.", group="soak", expected_any=("Stress Summary", "stress: ok=", "stress summary"), requires_opt_in="--include-soak", timeout_s=max(90.0, float(count) * 3.0)),
        CommandSpec(f"stress_mix {mix_count}", "Configured mixed-operation stress run.", group="soak", expected_any=("stress_mix summary", "Total:", "stress_mix"), requires_opt_in="--include-soak", timeout_s=max(90.0, float(mix_count) * 2.0)),
        CommandSpec("drv", "Final health after stress.", group="soak", expected_any=("Driver Health", "state=", "online="), validators=("driver_ready", "zero_failures"), requires_opt_in="--include-soak"),
        CommandSpec("settings", "Final settings after stress.", group="soak", expected_any=("=== Config ===", "state=", "mode="), validators=("driver_ready",), requires_opt_in="--include-soak"),
    ]


def clock_stretch_commands() -> list[CommandSpec]:
    return [
        CommandSpec("mode single", "Force single-shot mode.", group="clock-stretch", expected_any=("Status: OK", "mode: OK", "mode: OK code=0"), requires_opt_in="--include-clock-stretch", unsupported_ok=True),
        CommandSpec("stretch 1", "Enable clock stretching in driver settings.", group="clock-stretch", expected_any=("Status: OK", "stretch: OK", "stretch: OK code=0"), requires_opt_in="--include-clock-stretch", unsupported_ok=True),
        CommandSpec("meastime", "Record expected measurement time.", group="clock-stretch", expected_any=("Estimated measurement time:", "measurement_time_ms="), requires_opt_in="--include-clock-stretch"),
        CommandSpec("read", "Run stretch-enabled single-shot read using current settings.", group="clock-stretch", expected_any=("Temp:", "temperature=", "request: IN_PROGRESS"), validators=("measurement_plausible",), requires_opt_in="--include-clock-stretch", timeout_s=14.0, unsupported_ok=True),
        CommandSpec("serial stretch", "Read serial/EIC with clock stretching.", group="clock-stretch", expected_any=("Serial:", "serial=0x"), validators=("serial",), requires_opt_in="--include-clock-stretch", timeout_s=14.0, unsupported_ok=True),
        CommandSpec("stretch 0", "Restore no-stretch mode.", group="clock-stretch", expected_any=("Status: OK", "stretch: OK", "stretch: OK code=0"), requires_opt_in="--include-clock-stretch"),
        CommandSpec("single high", "Verify no-stretch single-shot path after restore.", group="clock-stretch", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), requires_opt_in="--include-clock-stretch", timeout_s=12.0),
        CommandSpec("serial nostretch", "Verify no-stretch serial path after restore.", group="clock-stretch", expected_any=("Serial:", "serial=0x"), validators=("serial",), requires_opt_in="--include-clock-stretch"),
    ]


def alert_write_commands() -> list[CommandSpec]:
    return [
        CommandSpec("alert show", "Record alert limits before write test.", group="alert-write", expected_any=("HIGH_SET", "alert_HIGH_SET", "raw=0x"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
        CommandSpec("alert encode 60 80", "Encode high-set value.", group="alert-write", expected_any=("Alert encoded:", "encoded=0x"), validators=("alert_encoded",), expected_encoded="0xCD33", requires_opt_in="--include-alert-write"),
        CommandSpec("alert decode 0xCD33", "Decode high-set value.", group="alert-write", expected_any=("Alert decoded:", "temperature="), validators=("alert_decoded",), expected_temperature_c=60.0, expected_humidity_pct=80.0, requires_opt_in="--include-alert-write"),
        CommandSpec("alert set hs 60 80", "Write high-set alert limit.", group="alert-write", expected_any=("Status: OK", "alert write: OK", "alert write: OK code=0"), requires_opt_in="--include-alert-write", destructive=True, timeout_s=12.0),
        CommandSpec("alert read hs", "Read high-set alert limit.", group="alert-write", expected_any=("Alert HIGH_SET:", "raw=0x", "alert_HIGH_SET"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
        CommandSpec("alert set hc 58 79", "Write high-clear alert limit.", group="alert-write", expected_any=("Status: OK", "alert write: OK", "alert write: OK code=0"), requires_opt_in="--include-alert-write", destructive=True, timeout_s=12.0),
        CommandSpec("alert read hc", "Read high-clear alert limit.", group="alert-write", expected_any=("Alert HIGH_CLEAR:", "raw=0x", "alert_HIGH_CLEAR"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
        CommandSpec("alert set lc -9 22", "Write low-clear alert limit.", group="alert-write", expected_any=("Status: OK", "alert write: OK", "alert write: OK code=0"), requires_opt_in="--include-alert-write", destructive=True, timeout_s=12.0),
        CommandSpec("alert read lc", "Read low-clear alert limit.", group="alert-write", expected_any=("Alert LOW_CLEAR:", "raw=0x", "alert_LOW_CLEAR"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
        CommandSpec("alert set ls -10 20", "Write low-set alert limit.", group="alert-write", expected_any=("Status: OK", "alert write: OK", "alert write: OK code=0"), requires_opt_in="--include-alert-write", destructive=True, timeout_s=12.0),
        CommandSpec("alert read ls", "Read low-set alert limit.", group="alert-write", expected_any=("Alert LOW_SET:", "raw=0x", "alert_LOW_SET"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
        CommandSpec("alert show", "Record alert limits after writes.", group="alert-write", expected_any=("HIGH_SET", "alert_HIGH_SET", "raw=0x"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
        CommandSpec("alert disable", "Disable alerts as cleanup.", group="alert-write", expected_any=("Status: OK", "alert disable: OK", "alert disable: OK code=0"), requires_opt_in="--include-alert-write", destructive=True, timeout_s=12.0, notes="Cleanup command; this still does not prove physical ALERT pin behavior."),
        CommandSpec("alert show", "Verify alert cleanup state.", group="alert-write", expected_any=("HIGH_SET", "alert_HIGH_SET", "raw=0x"), validators=("alert_read",), requires_opt_in="--include-alert-write", timeout_s=12.0),
    ]


def extra_periodic_commands() -> list[CommandSpec]:
    return [
        CommandSpec("periodic start 4 high", "Start 4 mps periodic acquisition.", group="periodic-all", expected_any=("periodic start: OK", "start_periodic: OK", "Status: OK", "periodic start: OK code=0"), requires_opt_in="--include-all-periodic-rates", recovery_command="periodic stop"),
        CommandSpec("periodic fetch", "Fetch one 4 mps periodic sample.", group="periodic-all", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), requires_opt_in="--include-all-periodic-rates", pre_delay_s=0.5, timeout_s=12.0),
        CommandSpec("periodic stop", "Stop 4 mps periodic acquisition.", group="periodic-all", expected_any=("periodic stop: OK", "stop_periodic: OK", "Status: OK", "periodic stop: OK code=0"), requires_opt_in="--include-all-periodic-rates"),
        CommandSpec("periodic start 10 high", "Start 10 mps periodic acquisition.", group="periodic-all", expected_any=("periodic start: OK", "start_periodic: OK", "Status: OK", "periodic start: OK code=0"), requires_opt_in="--include-all-periodic-rates", recovery_command="periodic stop"),
        CommandSpec("periodic fetch", "Fetch one 10 mps periodic sample.", group="periodic-all", expected_any=("Temp:", "temperature="), validators=("measurement_plausible",), requires_opt_in="--include-all-periodic-rates", pre_delay_s=0.35, timeout_s=12.0),
        CommandSpec("periodic stop", "Stop 10 mps periodic acquisition.", group="periodic-all", expected_any=("periodic stop: OK", "stop_periodic: OK", "Status: OK", "periodic stop: OK code=0"), requires_opt_in="--include-all-periodic-rates"),
    ]


def operator_specs() -> tuple[CommandSpec, ...]:
    return (
        CommandSpec("ALERT output/GPIO procedure", "Operator or GPIO-capture ALERT pin validation.", group="output-tests", send=False, operator_check=True, requires_opt_in="--include-output-tests", notes="Requires GPIO or logic-analyzer evidence. Without captured transitions this cannot be marked PASS."),
        CommandSpec("fault/unplug/CRC-injection procedure", "Fault validation procedure.", group="fault-tests", send=False, fixture_required=True, requires_opt_in="--include-fault-tests", notes="Requires safe jig, interposer, emulator, or deliberate unplug evidence. The runner does not fake faults."),
    )


FAIL_PATTERNS = [
    re.compile(r"\bERR\b"),
    re.compile(r"\bERROR\b"),
    re.compile(r"\bFAIL(?:ED)?\b"),
    re.compile(r"\bCRC_ERROR\b"),
    re.compile(r"\bCRC_MISMATCH\b"),
    re.compile(r"\bI2C_TIMEOUT\b"),
    re.compile(r"\bI2C_NACK(?:_[A-Z]+)?\b"),
    re.compile(r"\bDEVICE_NOT_FOUND\b"),
    re.compile(r"\bCOMMAND_FAILED\b"),
    re.compile(r"\bWRITE_CRC_ERROR\b"),
    re.compile(r"\bBUSY\b"),
    re.compile(r"\bOFFLINE\b"),
    re.compile(r"\bDEGRADED\b"),
    re.compile(r"Unknown command", re.IGNORECASE),
    re.compile(r"\bfail(?:ures)?\s*[:=]\s*(?!0\b)\d+", re.IGNORECASE),
]

UNSUPPORTED_PATTERNS = [
    re.compile(r"unsupported", re.IGNORECASE),
    re.compile(r"not supported", re.IGNORECASE),
    re.compile(r"\bINVALID_CONFIG\b"),
]


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def git_value(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args],
            cwd=repo_root(),
            text=True,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.strip()
    except Exception:
        return ""


def git_state() -> dict[str, str]:
    status = git_value("status", "--short")
    return {
        "branch": git_value("branch", "--show-current") or "unknown",
        "commit": git_value("rev-parse", "HEAD") or "unknown",
        "status_short": status,
        "worktree_clean": "false" if status else "true",
    }


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def iso_timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def default_executable_commands() -> list[str]:
    return [step.command for step in DEFAULT_COMMAND_SEQUENCE if step.send]


def opt_in_enabled(flag: str | None, args: argparse.Namespace) -> bool:
    if flag is None:
        return True
    return {
        "--include-destructive": args.include_destructive,
        "--include-soak": args.include_soak,
        "--include-fault-tests": args.include_fault_tests,
        "--include-output-tests": args.include_output_tests,
        "--include-clock-stretch": args.include_clock_stretch,
        "--include-alert-write": args.include_alert_write,
        "--include-all-periodic-rates": args.include_all_periodic_rates,
        "--include-bus-wide-reset": args.include_bus_wide_reset,
    }.get(flag, False)


def command_file(path: Path, timeout_s: float) -> list[CommandSpec]:
    out: list[CommandSpec] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(CommandSpec(line, "User-provided command.", group="custom", timeout_s=timeout_s, review_only=True, notes="No built-in acceptance criteria; review transcript."))
    return out


def with_timeout(spec: CommandSpec, timeout_s: float) -> CommandSpec:
    if timeout_s == DEFAULT_TIMEOUT_S:
        return spec
    return dataclasses.replace(spec, timeout_s=max(spec.timeout_s, timeout_s))


def all_optional_specs(args: argparse.Namespace) -> list[CommandSpec]:
    return [
        *destructive_commands(args.include_bus_wide_reset),
        *soak_commands(args.soak_count),
        *clock_stretch_commands(),
        *alert_write_commands(),
        *extra_periodic_commands(),
        *operator_specs(),
    ]


def build_plan(args: argparse.Namespace) -> tuple[list[CommandSpec], list[dict[str, str]]]:
    if args.commands:
        return command_file(Path(args.commands), args.timeout), []

    executable = [with_timeout(spec, args.timeout) for spec in DEFAULT_COMMAND_SEQUENCE]
    skipped: list[dict[str, str]] = []
    for spec in all_optional_specs(args):
        spec = with_timeout(spec, args.timeout)
        if opt_in_enabled(spec.requires_opt_in, args):
            executable.append(spec)
        else:
            skipped.append({
                "command": spec.command,
                "group": spec.group,
                "result": RESULT_SKIP,
                "reason": SKIP_NOT_SELECTED,
                "required_flag": spec.requires_opt_in or "",
                "notes": spec.notes,
            })
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
    if spec.unsupported_ok and any(pattern.search(plain) for pattern in UNSUPPORTED_PATTERNS):
        return False
    return any(token in plain for token in spec.failures) or any(pattern.search(plain) for pattern in FAIL_PATTERNS)


def has_unsupported(text: str, spec: CommandSpec) -> bool:
    if not spec.unsupported_ok:
        return False
    plain = strip_ansi(text)
    return any(pattern.search(plain) for pattern in UNSUPPORTED_PATTERNS) or "I2C_TIMEOUT" in plain


def expected(text: str, spec: CommandSpec) -> bool:
    plain = strip_ansi(text)
    all_ok = all(token in plain for token in spec.expected)
    any_ok = True if not spec.expected_any else any(token in plain for token in spec.expected_any)
    return (all_ok and any_ok) if (spec.expected or spec.expected_any) else bool(plain.strip())


def parse_i2c_addresses(command: str, text: str) -> list[str]:
    if command != "scan":
        return []
    addresses: set[str] = set()
    for line in strip_ansi(text).splitlines():
        if "Common addresses" in line:
            continue
        for match in re.finditer(r"\b0x([0-7][0-9A-Fa-f])\b", line):
            addresses.add(f"0x{match.group(1).upper()}")
        if re.match(r"^\s*[0-7][0-9A-Fa-f]:", line):
            for token in re.findall(r"\b([0-7][0-9A-Fa-f])\b", line.split(":", 1)[1]):
                addresses.add(f"0x{token.upper()}")
    return sorted(addresses)


def parse_command_output(command: str, text: str) -> dict[str, Any]:
    plain = strip_ansi(text)
    parsed: dict[str, Any] = {}

    addresses = parse_i2c_addresses(command, plain)
    if addresses:
        parsed["i2c_addresses_seen"] = addresses

    patterns = (
        ("library_version", r"SHT3x library version:\s*([^\r\n]+)"),
        ("library_version", r"library_version=([^\s]+)"),
        ("library_full", r"SHT3x library full:\s*([^\r\n]+)"),
        ("library_full", r"library_full=([^\s]+)"),
        ("firmware_version", r"Example firmware build:\s*([^\r\n]+)"),
        ("firmware_version", r"example_build=([^\r\n]+)"),
        ("library_commit", r"SHT3x library commit:\s*([^\r\n]+)"),
        ("library_commit", r"library_commit=([^\r\n]+)"),
    )
    for key, pattern in patterns:
        match = re.search(pattern, plain)
        if match:
            parsed[key] = match.group(1).strip()

    match = re.search(r"Temp:\s*(-?\d+(?:\.\d+)?)\s*C,\s*Humidity:\s*(-?\d+(?:\.\d+)?)\s*%", plain)
    if not match:
        match = re.search(r"temperature=(-?\d+(?:\.\d+)?)\s*C\s+humidity=(-?\d+(?:\.\d+)?)\s*%RH", plain)
    if match:
        parsed["temperature_c"] = float(match.group(1))
        parsed["humidity_pct"] = float(match.group(2))

    match = re.search(r"Raw:\s*T=0x([0-9A-Fa-f]{4})\s+RH=0x([0-9A-Fa-f]{4})", plain)
    if not match:
        match = re.search(r"rawT=0x([0-9A-Fa-f]{4})\s+rawRH=0x([0-9A-Fa-f]{4})", plain)
    if match:
        parsed["raw_temperature"] = f"0x{match.group(1).upper()}"
        parsed["raw_humidity"] = f"0x{match.group(2).upper()}"

    match = re.search(r"Comp:\s*T=(-?\d+)\s+\(x100\),\s*RH=(\d+)\s+\(x100\)", plain)
    if not match:
        match = re.search(r"tempC_x100=(-?\d+)\s+humidity(?:Pct)?_?x100=(\d+)", plain)
    if match:
        parsed["temp_c_x100"] = int(match.group(1))
        parsed["humidity_pct_x100"] = int(match.group(2))

    match = re.search(r"\b(?:status|Status raw):\s*(?:raw=)?0x([0-9A-Fa-f]{4})", plain)
    if not match:
        match = re.search(r"\bstatus=0x([0-9A-Fa-f]{4})", plain)
    if match:
        parsed["status_word"] = f"0x{match.group(1).upper()}"
    status_bits = re.search(
        r"alert=(\d)\s+heater=(\d)\s+rh_alert=(\d)\s+t_alert=(\d)\s+reset=(\d)\s+cmd_err=(\d)\s+crc_err=(\d)",
        plain,
    )
    if status_bits:
        parsed["status_bits"] = {
            "alert": int(status_bits.group(1)),
            "heater": int(status_bits.group(2)),
            "rh_alert": int(status_bits.group(3)),
            "t_alert": int(status_bits.group(4)),
            "reset": int(status_bits.group(5)),
            "cmd_err": int(status_bits.group(6)),
            "crc_err": int(status_bits.group(7)),
        }

    match = re.search(r"\b(?:Serial:\s*|serial=)0x([0-9A-Fa-f]{8})", plain)
    if match:
        parsed["serial_eic"] = f"0x{match.group(1).upper()}"

    match = re.search(r"\b(?:Heater:\s*(ON|OFF)|heater=(0|1))", plain)
    if match:
        parsed["heater"] = "OFF" if match.group(1) == "OFF" or match.group(2) == "0" else "ON"

    match = re.search(r"\b(?:State:\s*|state=)(UNINIT|READY|DEGRADED|OFFLINE)", plain)
    if match:
        parsed["state"] = match.group(1)
    match = re.search(r"\b(?:Online:\s*|online=)(true|false|YES|NO|0|1)", plain, re.IGNORECASE)
    if match:
        parsed["online"] = match.group(1).lower() in ("true", "yes", "1")
    match = re.search(r"Consecutive failures:\s*(\d+)", plain)
    if not match:
        match = re.search(r"\bconsec=(\d+)", plain)
    if match:
        parsed["consecutive_failures"] = int(match.group(1))
    match = re.search(r"Total success:\s*(\d+)", plain)
    if not match:
        match = re.search(r"\bok=(\d+)", plain)
    if match:
        parsed["total_success"] = int(match.group(1))
    match = re.search(r"Total failures:\s*(\d+)", plain)
    if not match:
        match = re.search(r"\bfail=(\d+)", plain)
    if match:
        parsed["total_failures"] = int(match.group(1))

    match = re.search(r"\b(?:Mode:\s*|mode=)(single|periodic|art|SINGLE_SHOT|PERIODIC|ART)", plain, re.IGNORECASE)
    if match:
        parsed["mode"] = match.group(1)
    match = re.search(r"\b(?:Repeatability:\s*|repeat=)(low|medium|med|high)", plain, re.IGNORECASE)
    if match:
        parsed["repeatability"] = match.group(1)
    match = re.search(r"\b(?:Periodic rate:\s*|rate=)(0\.5|0_5|1|2|4|10)", plain, re.IGNORECASE)
    if match:
        parsed["periodic_rate"] = match.group(1).replace("_", ".")
    match = re.search(r"\b(?:Clock stretching:\s*|stretch=)(ENABLED|DISABLED|0|1)", plain, re.IGNORECASE)
    if match:
        value = match.group(1).upper()
        parsed["clock_stretching"] = "ENABLED" if value in ("ENABLED", "1") else "DISABLED"

    encoded = re.search(r"(?:Alert encoded:\s*|encoded=)0x([0-9A-Fa-f]{4})", plain)
    if encoded:
        parsed["alert_encoded"] = f"0x{encoded.group(1).upper()}"
    decoded = re.search(r"(?:Alert decoded:\s*)?T=(-?\d+(?:\.\d+)?)C\s+RH=(-?\d+(?:\.\d+)?)%", plain)
    if not decoded:
        decoded = re.search(r"temperature=(-?\d+(?:\.\d+)?)\s+humidity=(-?\d+(?:\.\d+)?)", plain)
    if decoded and command.startswith("alert decode"):
        parsed["alert_decoded_temperature_c"] = float(decoded.group(1))
        parsed["alert_decoded_humidity_pct"] = float(decoded.group(2))

    alert_words = re.findall(r"(?:alert\s+|Alert\s+)(HIGH_SET|HIGH_CLEAR|LOW_CLEAR|LOW_SET)[: ]+.*?raw=0x([0-9A-Fa-f]{4})", plain)
    if alert_words:
        parsed["alert_limits"] = {name: f"0x{raw.upper()}" for name, raw in alert_words}
    elif command.startswith("alert read") and "raw=" in plain:
        match = re.search(r"raw=0x([0-9A-Fa-f]{4})", plain)
        if match:
            parsed["alert_raw"] = f"0x{match.group(1).upper()}"

    if "status_restore:" in plain:
        restore = re.search(r"initialMode=(\w+)\s+finalMode=(\w+)\s+modeInterrupted=(\d)\s+statusValid=(\d)\s+restored=(\d)", plain)
        if restore:
            parsed["status_restore"] = {
                "initialMode": restore.group(1),
                "finalMode": restore.group(2),
                "modeInterrupted": int(restore.group(3)),
                "statusValid": int(restore.group(4)),
                "restored": int(restore.group(5)),
            }
        for label in ("result", "stopStatus", "statusReadStatus", "restoreStatus"):
            match = re.search(rf"{label}:\s*(OK|ERR|IN_PROGRESS)\s+code=(\d+)", plain)
            if match:
                parsed.setdefault("status_restore_statuses", {})[label] = {
                    "kind": match.group(1),
                    "code": int(match.group(2)),
                }

    return parsed


def validate_parsed(spec: CommandSpec, parsed: dict[str, Any], args: argparse.Namespace) -> list[str]:
    errors: list[str] = []
    for validator in spec.validators:
        if validator == "version":
            if not parsed.get("library_version"):
                errors.append("library version not parsed")
        elif validator == "expected_address":
            expected_addr = str(args.expect_address).lower()
            seen = [str(addr).lower() for addr in parsed.get("i2c_addresses_seen", [])]
            if expected_addr not in seen:
                errors.append(f"expected address {args.expect_address} not seen")
        elif validator == "driver_ready":
            state = str(parsed.get("state", "")).upper()
            online = parsed.get("online")
            if state and state != "READY":
                errors.append(f"driver state is {state}, expected READY")
            if online is False:
                errors.append("driver is not online")
        elif validator == "zero_failures":
            if parsed.get("consecutive_failures", 0) != 0:
                errors.append("consecutive failures is nonzero")
            if parsed.get("total_failures", 0) != 0:
                errors.append("total failures is nonzero")
        elif validator == "status_word":
            if not parsed.get("status_word"):
                errors.append("status word not parsed")
        elif validator == "measurement_plausible":
            temp = parsed.get("temperature_c")
            humidity = parsed.get("humidity_pct")
            if temp is None or humidity is None:
                errors.append("measurement not parsed")
            else:
                if not (TEMP_MIN_C <= float(temp) <= TEMP_MAX_C):
                    errors.append(f"temperature {temp} C outside broad plausibility range")
                if not (HUMIDITY_MIN_PCT <= float(humidity) <= HUMIDITY_MAX_PCT):
                    errors.append(f"humidity {humidity} %RH outside broad plausibility range")
        elif validator == "raw_sample":
            if not parsed.get("raw_temperature") or not parsed.get("raw_humidity"):
                errors.append("raw sample not parsed")
        elif validator == "comp_sample":
            if "temp_c_x100" not in parsed or "humidity_pct_x100" not in parsed:
                errors.append("compensated sample not parsed")
        elif validator == "serial":
            if not parsed.get("serial_eic"):
                errors.append("serial/EIC not parsed")
        elif validator == "heater_off":
            if parsed.get("heater") != "OFF":
                errors.append("heater is not reported OFF")
        elif validator == "alert_read":
            if not parsed.get("alert_limits") and not parsed.get("alert_raw"):
                errors.append("alert limit raw value not parsed")
        elif validator == "alert_encoded":
            encoded = parsed.get("alert_encoded")
            if not encoded:
                errors.append("alert encoded value not parsed")
            elif spec.expected_encoded and str(encoded).upper() != spec.expected_encoded.upper():
                errors.append(f"alert encoded {encoded} != expected {spec.expected_encoded}")
        elif validator == "alert_decoded":
            temp = parsed.get("alert_decoded_temperature_c")
            humidity = parsed.get("alert_decoded_humidity_pct")
            if temp is None or humidity is None:
                errors.append("alert decoded value not parsed")
            else:
                if spec.expected_temperature_c is not None and abs(float(temp) - spec.expected_temperature_c) > spec.tolerance:
                    errors.append(f"decoded temperature {temp} C outside tolerance for {spec.expected_temperature_c} C")
                if spec.expected_humidity_pct is not None and abs(float(humidity) - spec.expected_humidity_pct) > spec.tolerance:
                    errors.append(f"decoded humidity {humidity} %RH outside tolerance for {spec.expected_humidity_pct} %RH")
        elif validator == "status_restore":
            snap = parsed.get("status_restore") or {}
            statuses = parsed.get("status_restore_statuses") or {}
            if snap.get("statusValid") != 1:
                errors.append("status_restore statusValid is not 1")
            if snap.get("restored") != 1:
                errors.append("status_restore restored is not 1")
            for label in ("result", "stopStatus", "statusReadStatus", "restoreStatus"):
                if label not in statuses:
                    errors.append(f"status_restore missing {label}")
                elif statuses[label].get("kind") not in ("OK", "IN_PROGRESS"):
                    errors.append(f"status_restore {label} is {statuses[label].get('kind')}")
    return errors


def classify(text: str, spec: CommandSpec, timed_out: bool, parsed: dict[str, Any], args: argparse.Namespace) -> tuple[str, str]:
    if spec.operator_check:
        return RESULT_OPERATOR, "OPERATOR_REVIEW_REQUIRED"
    if spec.fixture_required:
        return RESULT_SKIP, SKIP_REQUIRES_FIXTURE
    if timed_out:
        return RESULT_FAIL, "timeout"
    if has_unsupported(text, spec):
        return RESULT_SKIP, SKIP_UNSUPPORTED
    if has_failure(text, spec):
        return RESULT_FAIL, "failure token detected"
    if not expected(text, spec):
        return (RESULT_OPERATOR, "custom command requires review") if spec.review_only else (RESULT_FAIL, "expected output token missing")
    validation_errors = validate_parsed(spec, parsed, args)
    if validation_errors:
        return RESULT_FAIL, "; ".join(validation_errors)
    if spec.review_only:
        return RESULT_OPERATOR, "custom command requires review"
    return RESULT_PASS, ""


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


def result_row(spec: CommandSpec, result: str, reason: str, elapsed_s: float, output: str, completion: str, parsed: dict[str, Any]) -> dict[str, Any]:
    note_parts = [spec.notes] if spec.notes else []
    if reason:
        note_parts.append(reason)
    return {
        "command": spec.command,
        "purpose": spec.purpose,
        "group": spec.group,
        "result": result,
        "elapsed_s": round(elapsed_s, 3),
        "notes": " ".join(note_parts).strip(),
        "parsed": parsed,
        "completion_reason": completion,
        "output": output,
        "destructive": spec.destructive,
        "requires_opt_in": spec.requires_opt_in,
        "recovery_command": spec.recovery_command,
    }


def run_serial(ser: object, spec: CommandSpec, idle_s: float, args: argparse.Namespace) -> dict[str, Any]:
    if not spec.send:
        parsed: dict[str, Any] = {}
        result, reason = classify("", spec, False, parsed, args)
        return result_row(spec, result, reason, 0.0, "", "not-sent", parsed)

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
    parsed = parse_command_output(spec.command, output)
    result, note = classify(output, spec, reason == "timeout", parsed, args)
    return result_row(spec, result, note, time.monotonic() - start, output, reason, parsed)


def run_dry(spec: CommandSpec) -> dict[str, Any]:
    if spec.operator_check:
        result = RESULT_OPERATOR
        reason = "operator evidence required; dry-run did not execute"
    elif spec.fixture_required:
        result = RESULT_SKIP
        reason = SKIP_REQUIRES_FIXTURE
    else:
        result = RESULT_SKIP
        reason = SKIP_DRY_RUN
    return result_row(spec, result, reason, 0.0, "", "dry-run", {})


def verdict(results: list[dict[str, Any]], dry_run: bool) -> str:
    if dry_run:
        return VERDICT_INCOMPLETE
    values = {str(row["result"]) for row in results}
    if RESULT_FAIL in values:
        return VERDICT_FAIL
    if RESULT_OPERATOR in values:
        return VERDICT_OPERATOR
    if RESULT_SKIP in values:
        return VERDICT_INCOMPLETE
    return VERDICT_PASS if values == {RESULT_PASS} else VERDICT_INCOMPLETE


def aggregate_parsed(results: list[dict[str, Any]]) -> dict[str, Any]:
    aggregate: dict[str, Any] = {
        "i2c_addresses_seen": [],
        "firmware_version": "",
        "library_version": "",
        "library_full": "",
        "serial_eic": "",
        "final_health": {},
    }
    addresses: set[str] = set()
    for row in results:
        parsed = row.get("parsed") or {}
        for addr in parsed.get("i2c_addresses_seen", []):
            addresses.add(str(addr))
        for key in ("firmware_version", "library_version", "library_full", "serial_eic"):
            if parsed.get(key):
                aggregate[key] = parsed[key]
        if row.get("command") == "drv" and parsed:
            aggregate["final_health"] = {
                key: parsed[key]
                for key in ("state", "online", "consecutive_failures", "total_success", "total_failures")
                if key in parsed
            }
    aggregate["i2c_addresses_seen"] = sorted(addresses)
    return aggregate


def json_command(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "command": row["command"],
        "result": row["result"],
        "elapsed_s": row["elapsed_s"],
        "notes": row.get("notes", ""),
        "parsed": row.get("parsed", {}),
        "group": row.get("group", ""),
        "purpose": row.get("purpose", ""),
        "completion_reason": row.get("completion_reason", ""),
        "requires_opt_in": row.get("requires_opt_in"),
    }


def write_transcript(path: Path, args: argparse.Namespace, results: list[dict[str, Any]], initial_output: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("SHT3x serial HIL transcript\n")
        fh.write(f"timestamp_utc={iso_timestamp()}\nport={args.port or '<dry-run>'}\nbaud={args.baud}\ndry_run={int(args.dry_run)}\n\n")
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
            fh.write(f"===== RESULT: {row['result']} ({row['completion_reason']}) =====\n")
            if row.get("notes"):
                fh.write(f"===== NOTES: {row['notes']} =====\n")
            fh.write("\n")


def md_escape(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", "<br>")


def md_table(results: list[dict[str, Any]]) -> str:
    rows = ["| Command | Group | Purpose | Result | Completion | Elapsed s | Notes |",
            "| --- | --- | --- | --- | --- | ---: | --- |"]
    for row in results:
        rows.append(
            f"| `{md_escape(row['command'])}` | `{md_escape(row.get('group', ''))}` | "
            f"{md_escape(row['purpose'])} | `{row['result']}` | "
            f"{md_escape(row['completion_reason'])} | {row['elapsed_s']} | {md_escape(row.get('notes', ''))} |"
        )
    return "\n".join(rows)


def write_checklist(path: Path, skipped: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# SHT3x HIL Operator Checklist\n\n")
        fh.write("These checks are not proven unless the matching opt-in group runs and the required evidence is attached.\n\n")
        fh.write("| Item | Group | Result | Required flag | Evidence needed |\n| --- | --- | --- | --- | --- |\n")
        for row in skipped:
            fh.write(
                f"| `{md_escape(row['command'])}` | `{md_escape(row['group'])}` | `{row['reason']}` | "
                f"`{md_escape(row['required_flag'])}` | {md_escape(row['notes'])} |\n"
            )


def write_environment(path: Path, args: argparse.Namespace, state: dict[str, str], aggregate: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("SHT3x HIL environment\n")
        fh.write(f"timestamp_utc={iso_timestamp()}\n")
        fh.write(f"branch={state['branch']}\n")
        fh.write(f"commit={state['commit']}\n")
        fh.write(f"worktree_clean={state['worktree_clean']}\n")
        fh.write(f"port={args.port or '<dry-run>'}\n")
        fh.write(f"baud={args.baud}\n")
        fh.write(f"board={args.board}\n")
        fh.write(f"target_name={args.target_name}\n")
        fh.write(f"operator={args.operator}\n")
        fh.write(f"expected_address={args.expect_address}\n")
        fh.write(f"firmware_version={aggregate.get('firmware_version', '')}\n")
        fh.write(f"library_version={aggregate.get('library_version', '')}\n")


def write_operator_artifacts(log_dir: Path, args: argparse.Namespace) -> None:
    if not (args.include_output_tests or args.include_fault_tests):
        return
    (log_dir / "operator_notes.md").write_text(
        "# Operator Notes\n\nRecord manual observations, deviations, fixture setup, and cleanup state here.\n",
        encoding="utf-8",
    )
    (log_dir / "photos_or_evidence_manifest.md").write_text(
        "# Photos Or Evidence Manifest\n\nList attached photos, videos, scope captures, logic traces, and fixture files.\n",
        encoding="utf-8",
    )
    if args.include_output_tests:
        (log_dir / "alert_gpio_capture.csv").write_text("timestamp_s,level,notes\n", encoding="utf-8")
        (log_dir / "logic_analyzer_reference.txt").write_text(
            "Attach logic analyzer file path, channel mapping, sample rate, trigger, and idle/asserted levels.\n",
            encoding="utf-8",
        )


def write_summary_md(path: Path, args: argparse.Namespace, log_dir: Path, results: list[dict[str, Any]], skipped: list[dict[str, str]], final: str, state: dict[str, str], aggregate: dict[str, Any]) -> None:
    dirty = "clean" if state.get("worktree_clean") == "true" else "dirty"
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# SHT3x I2C HIL Summary\n\n")
        fh.write(f"Date/time UTC: {iso_timestamp()}\n\n")
        fh.write(f"Branch: `{state['branch']}`\n\nCommit: `{state['commit']}`\n\nWorktree state: `{dirty}`\n\n")
        fh.write(f"Serial port: `{args.port or '<dry-run>'}`\n\nBaud rate: `{args.baud}`\n\n")
        fh.write(f"Board: `{args.board}`\n\nTarget name: `{args.target_name}`\n\nOperator: `{args.operator}`\n\n")
        fh.write(f"Firmware version: `{aggregate.get('firmware_version') or '<not parsed>'}`\n\n")
        fh.write(f"Library version: `{aggregate.get('library_version') or '<not parsed>'}`\n\n")
        fh.write(f"Expected I2C address: `{args.expect_address}`\n\n")
        fh.write(f"I2C addresses seen: `{', '.join(aggregate.get('i2c_addresses_seen', [])) or '<none parsed>'}`\n\n")
        fh.write(f"Dry run: `{args.dry_run}`\n\n")
        fh.write("Runner boundary: host controls the repository diagnostic CLI over serial. ACK alone is not chip identity.\n\n")
        fh.write("## Configured Command Sequence\n\n")
        for row in results:
            fh.write(f"- `{row['command']}`\n")
        fh.write("\n## Per-Command Results\n\n")
        fh.write(md_table(results))
        fh.write("\n\n## Parsed Key Outputs\n\n")
        fh.write("```json\n")
        fh.write(json.dumps(aggregate, indent=2))
        fh.write("\n```\n\n")
        fh.write("## Skipped / Not Run\n\n")
        if skipped:
            for row in skipped:
                fh.write(f"- `{row['command']}` (`{row['group']}`): `{row['reason']}` via `{row['required_flag']}`. {row['notes']}\n")
        else:
            fh.write("- None recorded by this runner plan.\n")
        fh.write(f"\n## Final Verdict\n\n`{final}`\n\n")
        fh.write(f"## Claim Boundary\n\n{CLAIM_BOUNDARY}\n\n")
        fh.write("## Artifacts\n\n")
        for name in ("serial_transcript.txt", "summary.json", "operator_checklist.md", "environment.txt"):
            fh.write(f"- `{log_dir / name}`\n")
        if args.include_output_tests or args.include_fault_tests:
            for name in ("operator_notes.md", "photos_or_evidence_manifest.md", "alert_gpio_capture.csv", "logic_analyzer_reference.txt"):
                if (log_dir / name).exists():
                    fh.write(f"- `{log_dir / name}`\n")


def write_summary_json(path: Path, args: argparse.Namespace, log_dir: Path, results: list[dict[str, Any]], skipped: list[dict[str, str]], final: str, state: dict[str, str], initial_output: str, aggregate: dict[str, Any]) -> None:
    payload = {
        "timestamp_utc": iso_timestamp(),
        "branch": state["branch"],
        "commit": state["commit"],
        "worktree_clean": state["worktree_clean"] == "true",
        "port": args.port or "",
        "baud": args.baud,
        "board": args.board,
        "target_name": args.target_name,
        "operator": args.operator,
        "firmware_version": aggregate.get("firmware_version", ""),
        "library_version": aggregate.get("library_version", ""),
        "i2c_addresses_seen": aggregate.get("i2c_addresses_seen", []),
        "expected_address": args.expect_address,
        "dry_run": args.dry_run,
        "initial_serial_output_present": bool(initial_output),
        "commands": [json_command(row) for row in results],
        "skipped": skipped,
        "final_verdict": final,
        "claim_boundary": CLAIM_BOUNDARY,
        "artifacts": {
            "log_dir": str(log_dir),
            "serial_transcript": str(log_dir / "serial_transcript.txt"),
            "summary_md": str(log_dir / "summary.md"),
            "summary_json": str(path),
            "operator_checklist": str(log_dir / "operator_checklist.md"),
            "environment": str(log_dir / "environment.txt"),
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
    parser.add_argument("--include-output-tests", action="store_true")
    parser.add_argument("--include-fault-tests", action="store_true")
    parser.add_argument("--include-clock-stretch", action="store_true")
    parser.add_argument("--include-alert-write", action="store_true")
    parser.add_argument("--include-all-periodic-rates", action="store_true")
    parser.add_argument("--include-bus-wide-reset", action="store_true")
    parser.add_argument("--soak-count", type=int, default=100)
    parser.add_argument("--expect-address", default="0x44")
    parser.add_argument("--board", default="unspecified")
    parser.add_argument("--target-name", default="unspecified")
    parser.add_argument("--operator", default="unspecified")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.include_bus_wide_reset and not args.include_destructive:
        print("--include-bus-wide-reset requires --include-destructive", file=sys.stderr)
        return 2
    if args.include_bus_wide_reset:
        print("WARNING: --include-bus-wide-reset sends a general-call reset and may reset other I2C devices.", file=sys.stderr)
    if not args.dry_run and not args.port:
        print("--port is required unless --dry-run is used", file=sys.stderr)
        return 2

    log_dir = make_log_dir(Path(args.out))
    specs, skipped = build_plan(args)
    results: list[dict[str, Any]] = []
    initial_output = ""
    if args.dry_run:
        results = [run_dry(spec) for spec in specs]
    else:
        ser = open_serial(args)
        try:
            initial_output = drain_initial_output(ser, args.idle)
            for spec in specs:
                results.append(run_serial(ser, spec, args.idle, args))
        finally:
            ser.close()

    state = git_state()
    final = verdict(results, args.dry_run)
    aggregate = aggregate_parsed(results)
    write_transcript(log_dir / "serial_transcript.txt", args, results, initial_output)
    write_checklist(log_dir / "operator_checklist.md", skipped)
    write_environment(log_dir / "environment.txt", args, state, aggregate)
    write_operator_artifacts(log_dir, args)
    write_summary_md(log_dir / "summary.md", args, log_dir, results, skipped, final, state, aggregate)
    write_summary_json(log_dir / "summary.json", args, log_dir, results, skipped, final, state, initial_output, aggregate)
    print(f"HIL log directory: {log_dir}")
    print(f"Final verdict: {final}")
    print(f"Operator checklist: {log_dir / 'operator_checklist.md'}")
    if args.dry_run:
        print("Dry run only. No physical HIL validation was performed.")
    return 1 if final == VERDICT_FAIL else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
