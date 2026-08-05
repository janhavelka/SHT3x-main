#!/usr/bin/env python3
"""Check Arduino/native-IDF CLI parity and owner-safety invariants."""

from __future__ import annotations

import pathlib
import re
import sys

from sht3x_cli_contract import (
    COMMAND_SPECS,
    expected_help_rows,
    parse_help_rows,
    validate_contract,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]
REQUIRED_COMMON = (
    "BoardConfig.h",
    "I2cScanner.h",
    "I2cTransport.h",
    "Sht3xCli.h",
)


def fail(message: str) -> None:
    print(f"CLI contract FAILED: {message}")
    raise SystemExit(1)


def read(path: pathlib.Path, label: str) -> str:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")
    return path.read_text(encoding="utf-8", errors="replace")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        fail(f"{label} missing required token {token!r}")


def require_regex(source: str, pattern: str, label: str) -> None:
    if re.search(pattern, source, re.DOTALL) is None:
        fail(f"{label} missing required pattern {pattern!r}")


def compare_help(source: str, label: str) -> None:
    actual = parse_help_rows(source)
    expected = expected_help_rows()
    if actual == expected:
        return

    first = min(len(actual), len(expected))
    mismatch = next((index for index in range(first) if actual[index] != expected[index]), first)
    expected_row = expected[mismatch] if mismatch < len(expected) else "<end>"
    actual_row = actual[mismatch] if mismatch < len(actual) else "<end>"
    fail(
        f"{label} help differs from the authoritative contract at row {mismatch + 1}: "
        f"expected {expected_row!r}, got {actual_row!r}; "
        f"expected {len(expected)} rows, got {len(actual)}"
    )


def check_strict_parsing(source: str, label: str) -> None:
    for token in ("ERANGE", "std::strtoul", "std::strtof", "std::isfinite"):
        require(source, token, label)
    require_regex(source, r"end\s*==\s*(?:str|text)|end\s*==\s*token", label)
    require_regex(source, r"\*end\s*!=\s*'\\0'", label)


def check_confirmations(source: str, label: str) -> None:
    for spec in COMMAND_SPECS:
        if spec.safety == "CONFIRM_MUTATION":
            require(source, spec.synopsis, label)
    for token in ("greset arm", "greset disarm", "greset confirm"):
        require(source, token, label)
    require_regex(source, r"[Gg]eneral[Cc]all(?:Reset)?Armed|generalCallResetArmed", label)


def check_execution_metadata() -> None:
    by_id = {spec.command_id: spec for spec in COMMAND_SPECS}
    expected = {
        "REQUEST": "CORE_JOB",
        "FETCH": "CORE_JOB",
        "CANCEL": "CACHE_ONLY",
        "REPEAT": "BOUNDED_SYNC",
        "RATE": "BOUNDED_SYNC",
        "STRETCH": "CACHE_ONLY",
        "DEFAULTS": "BOUNDED_SYNC",
        "BEGIN": "LIFECYCLE",
        "END": "LIFECYCLE",
        "RECOVER": "CORE_JOB",
        "GRESET_CONFIRM": "RAW_I2C",
    }
    for command_id, execution in expected.items():
        actual = by_id[command_id].execution
        if actual != execution:
            fail(f"{command_id} execution class must be {execution}, got {actual}")


def main() -> int:
    contract_errors = validate_contract()
    if contract_errors:
        fail("invalid authoritative contract: " + "; ".join(contract_errors))
    check_execution_metadata()

    common = ROOT / "examples" / "common"
    bringup = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    arduino_cli = common / "Sht3xCli.cpp"
    arduino_header = common / "Sht3xCli.h"
    scanner = common / "I2cScanner.h"
    idf_main = ROOT / "examples" / "idf" / "basic" / "main" / "main.cpp"
    idf_transport_h = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.h"
    idf_transport_cpp = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.cpp"

    for filename in REQUIRED_COMMON:
        read(common / filename, f"common helper {filename}")
    bringup_text = read(bringup, "Arduino bring-up CLI")
    arduino_text = read(arduino_cli, "Arduino CLI")
    arduino_header_text = read(arduino_header, "Arduino CLI header")
    scanner_text = read(scanner, "Arduino I2C scanner")
    idf_text = read(idf_main, "native ESP-IDF CLI")
    idf_transport_text = read(idf_transport_h, "IDF transport header") + read(
        idf_transport_cpp, "IDF transport implementation"
    )

    require(bringup_text, "Sht3xCli.h", "Arduino bring-up")
    for token in ("cfg.nowMs", "cfg.nowUs", "cfg.cooperativeYield"):
        require(bringup_text, token, "Arduino bring-up")
    for token in (
        "i2c_scanner",
        "wire.setTimeOut(timeoutMs)",
        "addr < 0x08U || addr > 0x77U",
        "wire.endTransmission(true)",
        "0x44/0x45=SHT3x",
    ):
        require(scanner_text, token, "Arduino scanner")

    if "Sht3xCli.h" in idf_text:
        fail("native ESP-IDF CLI must not include Arduino example CLI source")
    for token in (
        "gConfig.nowMs",
        "gConfig.nowUs",
        "gConfig.cooperativeYield",
        'extern "C" void app_main(void)',
        "handleCommandLine",
        "std::fgets",
    ):
        require(idf_text, token, "native ESP-IDF CLI")

    compare_help(arduino_text, "Arduino CLI")
    compare_help(idf_text, "native ESP-IDF CLI")
    check_strict_parsing(arduino_text, "Arduino CLI")
    check_strict_parsing(idf_text, "native ESP-IDF CLI")
    check_confirmations(arduino_text, "Arduino CLI")
    check_confirmations(idf_text, "native ESP-IDF CLI")

    for token in (
        "requestMeasurement(request)",
        "pollJob(nowMs, 1, result)",
        "cancelJob(SHT3x::CancelReason::REQUESTED",
        "getMeasurementMilli(out)",
        "result.requestId != pendingRequestId",
        "requestEnsureIdle(request)",
        "beginOwnerSafe()",
        "deviceInstance.bind(configInstance)",
        "validateOwnedResult(cancelled, 0U, st)",
        "framework=",
        "idf_version=",
        "xfer_assert:",
        "Input line too long",
        "validCommandArity",
        "manualJobControl",
        "MANUAL_JOB_TIMEOUT_MS",
        "greset armed=1 zero_i2c=1",
        "greset armed=0 zero_i2c=1",
        "SHT3X_BUILD_TARGET",
    ):
        require(arduino_text + arduino_header_text + bringup_text, token, "Arduino CLI")
    for forbidden in ("deviceInstance.tick(", "deviceInstance.begin("):
        if forbidden in arduino_text:
            fail(f"Arduino CLI retains owner-unsafe lifecycle call {forbidden!r}")
    require_regex(arduino_text, r"if \(cmd == \"recover\"\).*?scheduleEnsureIdle\(\"recover\", false\)", "Arduino CLI")
    require_regex(arduino_text, r"if \(!ownerJobActive \|\| pendingRequestId == 0U\)\s*\{\s*Serial\.printf\(\"%s: none", "Arduino CLI")
    require_regex(arduino_text, r"validCommandArity\(parsed, knownCommand\).*?confirmationEffect\(parsed\).*?requireConfirmation\(cmd, parsed, effect\).*?if \(cmd == \"help\"", "Arduino CLI")
    require_regex(arduino_text, r"if \(cmd == \"settings\"\)\s*\{\s*printConfig\(true\)", "Arduino CLI")
    if arduino_text.count("cancelPending()") != 2:
        fail("Arduino CLI may cancel an owner job only from explicit job cancel/cancel dispatch")
    if arduino_text.count('"i2c_soak:') < 4:
        fail("Arduino duration-soak evidence must use bounded multi-record output")

    for token in (
        "SHT3x::JobRequest request",
        "request.requestId",
        "requestMeasurement(request)",
        "pollJob(nowMs(nullptr), budget, result)",
        "cancelOwnedJob(SHT3x::CancelReason::DEADLINE_EXPIRED",
        "result.requestId != requestId",
        "result.type != type",
        "result.effect",
        "framework=native-esp-idf",
        "esp_get_idf_version()",
        "CONFIG_IDF_TARGET",
        "xfer_assert",
        "validCommandArity",
        "Input line too long; discarded",
        "discardingOverflow",
        "Input queue full; discarded",
        "CLI_QUEUE_SEND_TIMEOUT_MS",
        "quarantineOwnerInvariant",
        "instructionLimit",
        "result.status.code != callStatus.code",
    ):
        require(idf_text, token, "native ESP-IDF CLI")
    for forbidden in ("gDevice.tick(", "gDevice.begin(", "gDevice.requestMeasurement()"):
        if forbidden in idf_text:
            fail(f"native ESP-IDF CLI retains identity-losing call {forbidden!r}")
    require_regex(idf_text, r"validCommandArity\(cmd, parsedArgs, knownCommand\).*?if \(std::strcmp\(cmd, \"help\"\)", "native ESP-IDF CLI")
    require_regex(idf_text, r"if \(!gOwner\.active\)\s*\{\s*std::puts\(\"fetch: ERR no_active_job=1\"\)", "native ESP-IDF CLI")
    if idf_text.count("hasExactConfirmation(") < 9:
        fail("native ESP-IDF CLI lost exact confirmation dispatch gates")

    for token in (
        "readTransfers",
        "writeTransfers",
        "totalTransfers",
        "incrementSaturated",
        "resetTransferCounters",
        "assertTransferCounters",
    ):
        require(idf_transport_text + idf_text, token, "native ESP-IDF transport")

    print(f"CLI contract PASSED ({len(COMMAND_SPECS)} authoritative help rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
