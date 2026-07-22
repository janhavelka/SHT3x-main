#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "TransportAdapter.h",
    "BusDiag.h",
    "CliShell.h",
    "CliStyle.h",
    "HealthView.h",
]

MANDATORY_COMMANDS = ["help", "scan", "probe", "recover", "drv", "read", "verbose", "stress"]
MANDATORY_HELP_ITEMS = [
    "help / ?",
    "version / ver",
    "mode [single|periodic|art]",
    "single <low|medium|high>",
    "periodic start <rate> <rep>",
    "periodic fetch",
    "periodic stop",
    "art start",
    "art fetch",
    "art stop",
    "start_periodic <rate> <rep>",
    "status_restore",
    "clear_status",
    "repeat [low|med|high]",
    "rate [0.5|1|2|4|10]",
    "stretch [0|1]",
    "command write <hex>",
    "command write_data <cmd> <data>",
    "command read <cmd> <len>",
    "alert show",
    "alert set <kind> <T> <RH>",
    "alert raw write <kind> <hex>",
    "cfg / settings",
    "stress_mix [N]",
    "selftest",
]


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def require_text(path: pathlib.Path, text: str, needle: str) -> None:
    if needle not in text:
        fail(f"missing '{needle}' in {path.as_posix()}")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    shared_cli = common_dir / "Sht3xCli.cpp"
    shared_cli_header = common_dir / "Sht3xCli.h"
    scanner = common_dir / "I2cScanner.h"
    idf_main = ROOT / "examples" / "idf" / "basic" / "main" / "main.cpp"

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")
    ensure_exists(shared_cli, "shared CLI implementation")
    ensure_exists(shared_cli_header, "shared CLI header")
    ensure_exists(scanner, "I2C scanner")
    ensure_exists(idf_main, "ESP-IDF CLI example")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    arduino_text = bringup_main.read_text(encoding="utf-8", errors="replace")
    idf_text = idf_main.read_text(encoding="utf-8", errors="replace")
    text = shared_cli.read_text(encoding="utf-8", errors="replace")
    scanner_text = scanner.read_text(encoding="utf-8", errors="replace")

    if "Sht3xCli.h" not in arduino_text:
        fail("Arduino example must use shared Sht3xCli.h")
    for token in ("cfg.nowMs", "cfg.nowUs", "cfg.cooperativeYield"):
        require_text(bringup_main, arduino_text, token)
    for token in (
        "i2c_scanner",
        "wire.setTimeOut(timeoutMs)",
        "addr < 0x08U || addr > 0x77U",
        "wire.endTransmission(true)",
        "Scan complete. Found %d device(s).",
        "0x44/0x45=SHT3x",
    ):
        require_text(scanner, scanner_text, token)
    if "Sht3xCli.h" in idf_text:
        fail("ESP-IDF example must not include shared Sht3xCli.h")
    for token in ("gConfig.nowMs", "gConfig.nowUs", "gConfig.cooperativeYield"):
        require_text(idf_main, idf_text, token)
    for token in ('extern "C" void app_main(void)', "handleCommandLine", "std::fgets"):
        if token not in idf_text:
            fail(f"ESP-IDF native CLI token missing: {token}")

    for cmd in MANDATORY_COMMANDS:
        if re.search(rf"\b{re.escape(cmd)}\b", text) is None:
            fail(f"mandatory command '{cmd}' missing in {shared_cli.as_posix()}")

    for token in (
        "requestMeasurement(request)",
        "pollJob(nowMs, 1, result)",
        "cancelJob(SHT3x::CancelReason::REQUESTED",
        "getMeasurementMilli(out)",
        "owner_api=pollJob milli=1",
        "current.requestId != pendingRequestId",
        "current.effect != SHT3x::JobEffect::NONE",
        "Measurement has physical effect; wait for terminal result",
    ):
        require_text(shared_cli, text, token)
    if "deviceInstance.tick(" in text:
        fail("Arduino CLI must retain PollJobResult instead of discarding it through tick()")
    if text.count('"i2c_soak:') < 4:
        fail("duration-soak evidence must use bounded multi-record output")

    for item in MANDATORY_HELP_ITEMS:
        if item not in text:
            fail(f"mandatory help item '{item}' missing in {shared_cli.as_posix()}")
        if item not in idf_text:
            fail(f"mandatory help item '{item}' missing in ESP-IDF native CLI")

    if re.search(r"\bcfg\b", text) is None and re.search(r"\bsettings\b", text) is None:
        fail("either 'cfg' or 'settings' command must be present")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
