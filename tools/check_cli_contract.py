#!/usr/bin/env python3
"""Check the shared Arduino/native-IDF CLI and owner-safety invariants."""

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
    shared_cli = common / "Sht3xCli.cpp"
    shared_header = common / "Sht3xCli.h"
    scanner = common / "I2cScanner.h"
    idf_main = ROOT / "examples" / "idf" / "basic" / "main" / "main.cpp"
    idf_transport_h = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.h"
    idf_transport_cpp = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.cpp"

    for filename in REQUIRED_COMMON:
        read(common / filename, f"common helper {filename}")
    bringup_text = read(bringup, "Arduino bring-up CLI")
    shared_text = read(shared_cli, "shared CLI")
    shared_header_text = read(shared_header, "shared CLI header")
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

    require(idf_text, "Sht3xCli.h", "native ESP-IDF glue")
    for forbidden in (
        "Arduino.h",
        "Wire.h",
        "TwoWire",
        "ArduinoCompat",
        "IdfArduinoCompat",
    ):
        if re.search(rf"\b{re.escape(forbidden)}\b", shared_text + shared_header_text):
            fail(f"shared CLI uses framework-specific token {forbidden!r}")
    if "Serial." in shared_text or "OutputProxy Serial" in shared_text:
        fail("shared CLI retains an Arduino-looking Serial output proxy")
    for token in (
        'extern "C" void app_main(void)',
        "sht3x_cli::setPlatform",
        "sht3x_cli::config()",
        "config.nowMs",
        "config.nowUs",
        "config.cooperativeYield",
        "sht3x_cli::beginOwnerSafe()",
        "sht3x_cli::processCommand",
        "sht3x_cli::tick()",
        "std::fgets",
        "std::clearerr(stdin)",
        "char chunk[INPUT_CHUNK_LEN]",
        "lineLength",
        "discardingOverflow",
        "Input line too long",
        "Input queue full",
        "CLI_QUEUE_SEND_TIMEOUT_MS",
        "external I2C pull-ups",
    ):
        require(idf_text, token, "native ESP-IDF glue")

    compare_help(shared_text, "shared CLI")
    check_strict_parsing(shared_text, "shared CLI")
    check_confirmations(shared_text, "shared CLI")

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
        require(shared_text + shared_header_text + bringup_text, token, "shared CLI")
    for forbidden in ("deviceInstance.tick(", "deviceInstance.begin("):
        if forbidden in shared_text:
            fail(f"shared CLI retains owner-unsafe lifecycle call {forbidden!r}")
    require_regex(shared_text, r"if \(cmd == \"recover\"\).*?scheduleEnsureIdle\(\"recover\", false\)", "shared CLI")
    require_regex(shared_text, r"if \(!ownerJobActive \|\| pendingRequestId == 0U\)\s*\{\s*output\.printf\(\"%s: none", "shared CLI")
    require_regex(shared_text, r"validCommandArity\(parsed, knownCommand\).*?confirmationEffect\(parsed\).*?requireConfirmation\(cmd, parsed, effect\).*?if \(cmd == \"help\"", "shared CLI")
    require_regex(shared_text, r"if \(cmd == \"settings\"\)\s*\{\s*printConfig\(true\)", "shared CLI")
    if shared_text.count("cancelPending()") != 2:
        fail("shared CLI may cancel an owner job only from explicit job cancel/cancel dispatch")
    if shared_text.count('"i2c_soak:') < 4:
        fail("shared duration-soak evidence must use bounded multi-record output")

    for token in (
        "native-esp-idf",
        "esp_get_idf_version()",
        "CONFIG_IDF_TARGET",
    ):
        require(idf_text + shared_text, token, "native ESP-IDF CLI")
    for forbidden in (
        "handleCommandLine",
        "validCommandArity",
        "printHelpItem",
        "SHT3x::SHT3x gDevice",
    ):
        if forbidden in idf_text:
            fail(f"native ESP-IDF glue duplicates shared CLI token {forbidden!r}")

    for token in (
        "readCallbacks",
        "writeCallbacks",
        "successes",
        "failures",
        "txBytes",
        "rxBytes",
        "saturatingAdd",
        "transferStats",
    ):
        require(idf_transport_text + idf_text + shared_text, token, "native ESP-IDF transport")

    print(f"CLI contract PASSED ({len(COMMAND_SPECS)} authoritative help rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
