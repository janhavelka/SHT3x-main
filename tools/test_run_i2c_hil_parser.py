#!/usr/bin/env python3
"""Small parser checks for tools/run_i2c_hil.py."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "run_i2c_hil.py"


def load_runner():
    spec = importlib.util.spec_from_file_location("run_i2c_hil_under_test", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot import run_i2c_hil.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


hil = load_runner()


class FakeSerial:
    def __init__(self, *chunks: str) -> None:
        self._chunks = [chunk.encode("utf-8") for chunk in chunks]
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> None:
        self.writes.append(data)

    def flush(self) -> None:
        pass

    def read(self, _size: int) -> bytes:
        return self._chunks.pop(0) if self._chunks else b""


class AsyncSampleSerial:
    def __init__(self) -> None:
        self.writes: list[bytes] = []
        self._scheduled_sent = False
        self._sample_sent = False

    def write(self, data: bytes) -> None:
        self.writes.append(data)

    def flush(self) -> None:
        pass

    def read(self, _size: int) -> bytes:
        if not self._scheduled_sent:
            self._scheduled_sent = True
            return b"periodic fetch: IN_PROGRESS code=10 detail=0 msg=Measurement scheduled\n> "
        if len(self.writes) > 1 and not self._sample_sent:
            self._sample_sent = True
            return b"Temp: 27.81 C, Humidity: 42.95 %\n"
        return b""


class ReadErrorSerial:
    def __init__(self) -> None:
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> None:
        self.writes.append(data)

    def flush(self) -> None:
        pass

    def read(self, _size: int) -> bytes:
        raise OSError("synthetic serial read failure")


def fake_args() -> argparse.Namespace:
    return argparse.Namespace(expect_address="0x44")


def classify(spec, text: str):
    parsed = hil.parse_command_output(spec.command, text)
    result, notes = hil.classify(text, spec, False, parsed, fake_args())
    return result, notes, parsed


def test_pass_command() -> None:
    spec = hil.version_step()
    result, notes, parsed = classify(
        spec,
        "=== Version Info ===\n  SHT3x library version: 1.5.0\n  SHT3x library full: 1.5.0+abc\n",
    )
    assert result == hil.RESULT_PASS, notes
    assert parsed["library_version"] == "1.5.0"


def test_default_sequence_covers_common_minimum_commands() -> None:
    commands = hil.default_executable_commands()
    for command in ("version", "scan", "probe", "settings", "drv"):
        assert command in commands, command


def test_unknown_command_fails() -> None:
    spec = hil.CommandSpec("bogus", "test", expected_any=("OK",))
    result, notes, _ = classify(spec, "Unknown command. Try 'help'.\n")
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_unknown_command_failure_takes_precedence_over_expected_token() -> None:
    spec = hil.CommandSpec("bogus", "test", expected_any=("OK",))
    result, notes, _ = classify(spec, "OK\nUnknown command. Try 'help'.\n")
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_failure_tokens_fail() -> None:
    spec = hil.CommandSpec("probe", "failure-token", expected_any=("Status:",))
    for token in (
        "FAIL",
        "FAILED",
        "ERR",
        "ERROR",
        "CRC_ERROR",
        "CRC_MISMATCH",
        "I2C_TIMEOUT",
        "I2C_NACK",
        "DEVICE_NOT_FOUND",
        "COMMAND_FAILED",
        "WRITE_CRC_ERROR",
        "BUSY",
        "OFFLINE",
        "DEGRADED",
        "fail=1",
    ):
        result, notes, _ = classify(spec, f"Status: OK\n{token}\n")
        assert result == hil.RESULT_FAIL, token
        assert "failure token" in notes, token


def test_i2c_nack_suffix_fails() -> None:
    spec = hil.CommandSpec("probe", "failure-token", expected_any=("Status:",))
    result, notes, _ = classify(spec, "Status: OK\nI2C_NACK_ADDR\n")
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_expected_in_progress_and_measurement_scheduled_do_not_fail() -> None:
    spec = hil.CommandSpec(
        "read",
        "scheduled read",
        expected_any=("Measurement scheduled", "request: IN_PROGRESS"),
    )
    result, notes, _ = classify(spec, "Measurement scheduled\nrequest: IN_PROGRESS code=8\n")
    assert result == hil.RESULT_PASS, notes


def test_missing_expected_address_fails() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "scan")
    result, notes, parsed = classify(spec, "I2C scan:\n  0x45\nScan complete\n")
    assert parsed["i2c_addresses_seen"] == ["0x45"]
    assert result == hil.RESULT_FAIL
    assert "expected address 0x44 not seen" in notes


def test_scan_parser_ignores_known_address_documentation() -> None:
    parsed = hil.parse_command_output(
        "scan",
        "I2C scan:\n"
        "  Found device at 0x3C\n"
        "  Found device at 0x44 (0x44/0x45=SHT3x)\n"
        "Known: 0x44/0x45=SHT3x, 0x76/0x77=BME280/BMP280\n",
    )
    assert parsed["i2c_addresses_seen"] == ["0x3C", "0x44"]


def test_configured_address_mismatch_fails() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "settings")
    result, notes, parsed = classify(spec, "state=READY initialized=1 online=1 addr=0x45 timeout=25\nmode=single\n")
    assert parsed["configured_i2c_address"] == "0x45"
    assert result == hil.RESULT_FAIL
    assert "configured I2C address 0x45 != expected 0x44" in notes


def test_configured_address_missing_fails() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "settings")
    result, notes, _ = classify(spec, "state=READY initialized=1 online=1 timeout=25\nmode=single\n")
    assert result == hil.RESULT_FAIL
    assert "configured I2C address not parsed" in notes


def test_status_output_parses() -> None:
    parsed = hil.parse_command_output(
        "status",
        "status: raw=0x8010 alert=1 heater=0 rh_alert=0 t_alert=0 reset=1 cmd_err=0 crc_err=0\n",
    )
    assert parsed["status_word"] == "0x8010"
    assert parsed["status_bits"]["alert"] == 1
    assert parsed["status_bits"]["reset"] == 1


def test_status_raw_idf_output_parses() -> None:
    parsed = hil.parse_command_output("status_raw", "status=0x8010\n")
    assert parsed["status_word"] == "0x8010"


def test_missing_status_word_fails() -> None:
    spec = hil.CommandSpec("status_raw", "status", expected_any=("status",), validators=("status_word",))
    result, notes, _ = classify(spec, "status unavailable\n")
    assert result == hil.RESULT_FAIL
    assert "status word not parsed" in notes


def test_measurement_parses_and_passes() -> None:
    spec = hil.CommandSpec(
        "single high",
        "measurement",
        expected_any=("Temp:",),
        validators=("measurement_plausible",),
    )
    result, notes, parsed = classify(spec, "single request: IN_PROGRESS code=8\nTemp: 26.88 C, Humidity: 50.10 %\n")
    assert result == hil.RESULT_PASS, notes
    assert parsed["temperature_c"] == 26.88
    assert parsed["humidity_pct"] == 50.10


def test_idf_measurement_format_parses_and_passes() -> None:
    spec = hil.CommandSpec(
        "single high",
        "measurement",
        expected_any=("temperature=",),
        validators=("measurement_plausible",),
    )
    result, notes, parsed = classify(spec, "temperature=23.50 C humidity=44.25 %RH\n")
    assert result == hil.RESULT_PASS, notes
    assert parsed["temperature_c"] == 23.50
    assert parsed["humidity_pct"] == 44.25


def test_measurement_plausibility_failure() -> None:
    spec = hil.CommandSpec(
        "single high",
        "measurement",
        expected_any=("temperature=",),
        validators=("measurement_plausible",),
    )
    result, notes, _ = classify(spec, "temperature=130.00 C humidity=120.00 %RH\n")
    assert result == hil.RESULT_FAIL
    assert "temperature 130.0 C outside broad plausibility range" in notes
    assert "humidity 120.0 %RH outside broad plausibility range" in notes


def test_driver_health_parses_and_passes() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("Driver Health",),
        validators=("driver_ready", "zero_failures"),
    )
    result, notes, parsed = classify(
        spec,
        "=== Driver Health ===\n  State: READY\n  Online: true\n"
        "  Consecutive failures: 0\n  Total success: 24\n  Total failures: 0\n",
    )
    assert result == hil.RESULT_PASS, notes
    assert parsed["state"] == "READY"
    assert parsed["online"] is True
    assert parsed["total_failures"] == 0


def test_idf_driver_health_consecutive_parses_and_passes() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("health ok=",),
        validators=("zero_failures",),
    )
    result, notes, parsed = classify(spec, "health ok=24 fail=0 consecutive=0 lastOk=100 lastErr=0\n")
    assert result == hil.RESULT_PASS, notes
    assert parsed["total_success"] == 24
    assert parsed["total_failures"] == 0
    assert parsed["consecutive_failures"] == 0


def test_idf_driver_health_full_format_parses_and_passes() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("state=",),
        validators=("driver_ready", "zero_failures"),
    )
    result, notes, parsed = classify(
        spec,
        "getSettings: OK code=0 detail=0 msg=\n"
        "state=READY initialized=1 online=1 addr=0x44 timeout=100\n"
        "mode=single repeat=high rate=1 stretch=0 periodic=0 pending=0 ready=0 sample=0\n"
        "health ok=24 fail=0 consecutive=0 lastOk=100 lastErr=0\n",
    )
    assert result == hil.RESULT_PASS, notes
    assert parsed["state"] == "READY"
    assert parsed["online"] is True
    assert parsed["configured_i2c_address"] == "0x44"
    assert parsed["total_success"] == 24
    assert parsed["total_failures"] == 0
    assert parsed["consecutive_failures"] == 0


def test_malformed_driver_state_fails() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("state=",),
        validators=("driver_ready",),
    )
    result, notes, parsed = classify(spec, "state=BOGUS online=1\n")
    assert parsed["invalid_state"] == "BOGUS"
    assert result == hil.RESULT_FAIL
    assert "driver state is unrecognized" in notes


def test_driver_health_missing_online_fails_for_drv() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("Driver Health",),
        validators=("driver_ready",),
    )
    result, notes, _ = classify(spec, "=== Driver Health ===\n  State: READY\n")
    assert result == hil.RESULT_FAIL
    assert "driver online flag not parsed" in notes


def test_driver_health_offline_flag_fails() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("Driver Health",),
        validators=("driver_ready",),
    )
    result, notes, _ = classify(spec, "=== Driver Health ===\n  State: READY\n  Online: false\n")
    assert result == hil.RESULT_FAIL
    assert "driver is not online" in notes


def test_status_restore_snapshot_parses() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "status_restore")
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=periodic finalMode=periodic modeInterrupted=1 statusValid=1 restored=1\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: OK code=0 detail=0 msg=\n"
        "restoreStatus: OK code=0 detail=0 msg=\n"
        "status: raw=0x8010 alert=1 heater=0 rh_alert=0 t_alert=0 reset=1 cmd_err=0 crc_err=0\n"
    )
    result, notes, parsed = classify(spec, text)
    assert result == hil.RESULT_PASS, notes
    assert parsed["status_restore"]["restored"] == 1
    assert parsed["status_restore_statuses"]["statusReadStatus"]["kind"] == "OK"


def test_status_restore_boolean_snapshot_parses() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "status_restore")
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=periodic finalMode=periodic modeInterrupted=true statusValid=1 restored=true\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: OK code=0 detail=0 msg=\n"
        "restoreStatus: OK code=0 detail=0 msg=\n"
    )
    result, notes, parsed = classify(spec, text)
    assert result == hil.RESULT_PASS, notes
    assert parsed["status_restore"]["modeInterrupted"] == 1
    assert parsed["status_restore"]["statusValid"] == 1
    assert parsed["status_restore"]["restored"] == 1


def test_missing_status_restore_snapshot_fails() -> None:
    spec = hil.CommandSpec(
        "status_restore",
        "status restore",
        expected_any=("status_restore:",),
        validators=("status_restore",),
    )
    result, notes, _ = classify(spec, "status_restore:\n")
    assert result == hil.RESULT_FAIL
    assert "status_restore statusValid is not 1" in notes
    assert "status_restore restored is not 1" in notes
    assert "status_restore missing stopStatus" in notes


def test_status_restore_missing_fields_fails() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "status_restore")
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=periodic finalMode=periodic modeInterrupted=1 statusValid=1 restored=1\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: OK code=0 detail=0 msg=\n"
    )
    result, notes, _ = classify(spec, text)
    assert result == hil.RESULT_FAIL
    assert "status_restore missing restoreStatus" in notes


def test_status_restore_invalid_snapshot_fails() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "status_restore")
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=periodic finalMode=periodic modeInterrupted=1 statusValid=0 restored=0\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: OK code=0 detail=0 msg=\n"
        "restoreStatus: OK code=0 detail=0 msg=\n"
    )
    result, notes, _ = classify(spec, text)
    assert result == hil.RESULT_FAIL
    assert "status_restore statusValid is not 1" in notes
    assert "status_restore restored is not 1" in notes


def test_status_restore_err_substatus_fails() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "status_restore")
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=periodic finalMode=periodic modeInterrupted=1 statusValid=1 restored=1\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: ERR code=4 detail=0 msg=\n"
        "restoreStatus: OK code=0 detail=0 msg=\n"
    )
    result, notes, _ = classify(spec, text)
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_status_restore_active_validator_fails_when_not_interrupted() -> None:
    spec = hil.CommandSpec(
        "status_restore",
        "status restore active",
        expected=("status_restore:", "statusReadStatus", "statusValid=1"),
        expected_any=("restored=1",),
        validators=("status_restore", "status_restore_active"),
    )
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=single finalMode=single modeInterrupted=0 statusValid=1 restored=1\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: OK code=0 detail=0 msg=\n"
        "restoreStatus: OK code=0 detail=0 msg=\n"
    )
    result, notes, _ = classify(spec, text)
    assert result == hil.RESULT_FAIL
    assert "expected periodic or art" in notes
    assert "modeInterrupted is not 1" in notes


def test_status_restore_active_validator_requires_mode_restore() -> None:
    spec = hil.CommandSpec(
        "status_restore",
        "status restore active",
        expected=("status_restore:", "statusReadStatus", "statusValid=1"),
        expected_any=("restored=1",),
        validators=("status_restore", "status_restore_active"),
    )
    text = (
        "status_restore:\n"
        "result: OK code=0 detail=0 msg=\n"
        "initialMode=periodic finalMode=single modeInterrupted=1 statusValid=1 restored=1\n"
        "stopStatus: OK code=0 detail=0 msg=\n"
        "statusReadStatus: OK code=0 detail=0 msg=\n"
        "restoreStatus: OK code=0 detail=0 msg=\n"
    )
    result, notes, _ = classify(spec, text)
    assert result == hil.RESULT_FAIL
    assert "status_restore finalMode is single, expected periodic" in notes


def test_final_driver_failures_fail() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("Driver Health",),
        validators=("driver_ready", "zero_failures"),
    )
    result, notes, parsed = classify(
        spec,
        "=== Driver Health ===\n  State: READY\n  Online: true\n"
        "  Consecutive failures: 1\n  Total success: 24\n  Total failures: 2\n",
    )
    assert parsed["consecutive_failures"] == 1
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_final_driver_failure_counters_are_required() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("Driver Health",),
        validators=("driver_ready", "zero_failures"),
    )
    result, notes, _ = classify(spec, "=== Driver Health ===\n  State: READY\n  Online: true\n")
    assert result == hil.RESULT_FAIL
    assert "consecutive failures not parsed" in notes
    assert "total failures not parsed" in notes


def test_zero_failures_validator_catches_compact_nonzero_consecutive() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health",
        expected_any=("health ok=",),
        validators=("zero_failures",),
    )
    result, notes, parsed = classify(spec, "health ok=24 fail=0 consecutive=1 lastOk=100 lastErr=0\n")
    assert parsed["consecutive_failures"] == 1
    assert result == hil.RESULT_FAIL
    assert "consecutive failures is nonzero" in notes


def test_dry_run_verdict_is_incomplete() -> None:
    results = [hil.run_dry(spec) for spec in hil.DEFAULT_COMMAND_SEQUENCE[:2]]
    assert all(row["result"] == hil.RESULT_SKIP for row in results)
    assert hil.verdict(results, dry_run=True) == hil.VERDICT_INCOMPLETE


def test_dry_run_operator_and_fixture_branches() -> None:
    operator_row = hil.run_dry(hil.operator_specs()[0])
    fixture_row = hil.run_dry(hil.operator_specs()[1])
    assert operator_row["result"] == hil.RESULT_OPERATOR
    assert "operator evidence required; dry-run did not execute" in operator_row["notes"]
    assert fixture_row["result"] == hil.RESULT_SKIP
    assert hil.SKIP_REQUIRES_FIXTURE in fixture_row["notes"]


def test_recovery_spec_is_generated_for_failed_step() -> None:
    spec = next(item for item in hil.DEFAULT_COMMAND_SEQUENCE if item.command == "periodic start 1 high")
    recovery = hil.recovery_spec_for(spec)
    assert recovery is not None
    assert recovery.command == "periodic stop"
    assert recovery.group == "default-recovery"
    assert "periodic stop: OK" in recovery.expected_any


def test_stress_mix_requires_mixed_summary_not_plain_stress() -> None:
    spec = hil.soak_commands(10)[2]
    assert spec.command.startswith("stress_mix ")
    assert "stress: ok=" not in spec.expected_any
    assert "stress_mix" not in spec.expected_any
    result, notes, _ = classify(spec, "stress: ok=5 fail=0\n")
    assert result == hil.RESULT_FAIL
    assert "stress total 5 != requested 5" not in notes


def test_stress_mix_plain_token_is_not_completion_token() -> None:
    spec = hil.soak_commands(10)[2]
    result, notes, parsed = classify(spec, "stress_mix\n")
    assert parsed == {}
    assert result == hil.RESULT_FAIL
    assert "stress totals not parsed" in notes


def test_stress_parser_prefers_final_summary_over_progress() -> None:
    parsed = hil.parse_command_output(
        "stress 500",
        """
  Progress: 50/500 (10%, ok=50, fail=0)
=== Stress Summary ===
  Target: 500
  Attempts: 500
  Success: 500
  Errors: 0
""",
    )
    assert parsed["total_success"] == 500
    assert parsed["total_failures"] == 0


def test_stress_mix_parser_prefers_final_summary_over_progress() -> None:
    parsed = hil.parse_command_output(
        "stress_mix 250",
        """
  Progress: 25/250 (10%, ok=25, fail=0)
=== stress_mix summary ===
  Total: ok=250 fail=0 (100.00%)
""",
    )
    assert parsed["total_success"] == 250
    assert parsed["total_failures"] == 0


def test_stress_mix_parser_accepts_compact_summary() -> None:
    parsed = hil.parse_command_output(
        "stress_mix 250",
        "stress_mix: ok=250 fail=0 duration_ms=512\n",
    )
    assert parsed["total_success"] == 250
    assert parsed["total_failures"] == 0
    assert parsed["duration_ms"] == 512


def test_stress_parser_accepts_compact_summary_metadata() -> None:
    parsed = hil.parse_command_output(
        "stress 500",
        "stress: ok=500 fail=0 attempts=500 target=500 duration_ms=9123\n",
    )
    assert parsed["total_success"] == 500
    assert parsed["total_failures"] == 0
    assert parsed["attempts"] == 500
    assert parsed["target"] == 500
    assert parsed["duration_ms"] == 9123


def test_stress_validator_rejects_nonzero_failures() -> None:
    spec = hil.soak_commands(500)[1]
    result, notes, parsed = classify(
        spec,
        "stress: ok=499 fail=1 attempts=500 target=500 duration_ms=10000\n",
    )
    assert parsed["total_failures"] == 1
    assert result == hil.RESULT_FAIL
    assert "failure token detected" in notes or "stress failures is nonzero" in notes


def test_stress_validator_rejects_incomplete_totals() -> None:
    spec = hil.soak_commands(500)[1]
    result, notes, parsed = classify(
        spec,
        "stress: ok=499 fail=0 attempts=499 target=500 duration_ms=10000\n",
    )
    assert parsed["total_success"] == 499
    assert result == hil.RESULT_FAIL
    assert "stress total 499 != requested 500" in notes


def test_stress_mix_validator_rejects_nonzero_failures() -> None:
    spec = hil.soak_commands(500)[2]
    result, notes, parsed = classify(spec, "stress_mix: ok=249 fail=1 duration_ms=512\n")
    assert parsed["total_failures"] == 1
    assert result == hil.RESULT_FAIL
    assert "failure token detected" in notes or "stress failures is nonzero" in notes


def test_stress_mix_timeout_parser_uses_last_progress() -> None:
    parsed = hil.parse_command_output(
        "stress_mix 250",
        """
  Progress: 25/250 (10%, ok=25, fail=0)
  Progress: 225/250 (90%, ok=225, fail=0)
""",
    )
    assert parsed["progress_completed"] == 225
    assert parsed["progress_total"] == 250
    assert parsed["total_success"] == 225
    assert parsed["total_failures"] == 0


def test_timeout_notes_include_last_stress_mix_window() -> None:
    spec = hil.CommandSpec("stress_mix 250", "mixed")
    row = hil.result_row(
        spec,
        hil.RESULT_FAIL,
        "timeout",
        500.0,
        "Progress: 225/250 (90%, ok=225, fail=0)\n",
        "timeout",
        {},
    )
    assert "last progress 225/250" in row["notes"]
    assert "next window 226-250" in row["notes"]
    assert "226:readStatus" in row["notes"]


def test_duration_soak_plan_uses_warmup_only() -> None:
    specs = hil.soak_commands(25, duration_s=8.0)
    assert len(specs) == 1
    assert specs[0].command == "stress 10"
    assert "warmup" in specs[0].purpose


def test_i2c_soak_command_uses_low_usb_firmware_path() -> None:
    spec = hil.i2c_soak_command(57600.0)
    assert spec.command == "i2c_soak 57600"
    assert spec.group == "duration-soak"
    assert spec.timeout_s > 57600.0
    assert spec.timeout_s <= 58200.0
    assert spec.expected_any == hil.I2C_SOAK_EXPECTED


def test_i2c_soak_parser_accepts_zero_failure_summary() -> None:
    spec = hil.i2c_soak_command(1.0)
    result, notes, parsed = classify(
        spec,
        "i2c_soak: ok=64 fail=0 duration_ms=1005 temp_min=24.10 "
        "temp_max=24.40 humidity_min=44.00 humidity_max=44.50 "
        "health_ok_delta=64 health_fail_delta=0 state=READY consec=0\n",
    )
    assert result == hil.RESULT_PASS, notes
    assert parsed["total_success"] == 64
    assert parsed["total_failures"] == 0
    assert parsed["health_fail_delta"] == 0
    assert parsed["state"] == "READY"


def test_i2c_soak_parser_rejects_health_failure() -> None:
    spec = hil.i2c_soak_command(1.0)
    result, notes, parsed = classify(
        spec,
        "i2c_soak: ok=63 fail=1 duration_ms=1005 temp_min=24.10 "
        "temp_max=24.40 humidity_min=44.00 humidity_max=44.50 "
        "health_ok_delta=63 health_fail_delta=1 state=DEGRADED consec=1\n",
    )
    assert result == hil.RESULT_FAIL
    assert parsed["total_failures"] == 1
    assert "failure token detected" in notes or "i2c_soak failures is nonzero" in notes


def test_duration_soak_cycle_specs_are_bounded() -> None:
    args = argparse.Namespace(
        soak_chunk_count=8,
        soak_recover_every=0,
        timeout=hil.DEFAULT_TIMEOUT_S,
    )
    specs = hil.duration_soak_cycle_specs(args, cycle=6)
    commands = [spec.command for spec in specs]
    assert "stress 8" in commands
    assert "stress_mix 4" in commands
    assert "periodic start 10 high" in commands
    assert "art start" in commands
    assert "settings" not in commands
    assert all(spec.timeout_s >= hil.DEFAULT_TIMEOUT_S for spec in specs)


def test_parser_self_test() -> None:
    assert hil.parser_self_test() == 0


def test_serial_unsupported_response_skips_without_timeout() -> None:
    spec = hil.CommandSpec(
        "art start",
        "unsupported art",
        expected_any=("art start: OK",),
        unsupported_ok=True,
        timeout_s=0.1,
    )
    row = hil.run_serial(FakeSerial("art start: unsupported\n"), spec, idle_s=0.0, args=fake_args())
    assert row["result"] == hil.RESULT_SKIP
    assert row["notes"] == hil.SKIP_UNSUPPORTED
    assert row["completion_reason"] == "unsupported-token+idle"


def test_async_measurement_wait_sends_online_nudge() -> None:
    spec = hil.CommandSpec(
        "periodic fetch",
        "async periodic fetch",
        expected_any=("Temp:", "temperature="),
        validators=("measurement_plausible",),
        timeout_s=1.0,
    )
    ser = AsyncSampleSerial()
    row = hil.run_serial(ser, spec, idle_s=0.0, args=fake_args())
    assert row["result"] == hil.RESULT_PASS, row["notes"]
    assert row["parsed"]["temperature_c"] == 27.81
    assert len(ser.writes) >= 2
    assert ser.writes[1] == b"online\n"


def test_serial_read_exception_returns_fail_row() -> None:
    spec = hil.CommandSpec("drv", "health", expected_any=("Driver Health",))
    row = hil.run_serial(ReadErrorSerial(), spec, idle_s=0.0, args=fake_args())
    assert row["result"] == hil.RESULT_FAIL
    assert row["completion_reason"] == "serial-exception"
    assert "serial read exception" in row["notes"]


def test_health_commands_wait_for_prompt_before_completion() -> None:
    spec = hil.CommandSpec(
        "settings",
        "config snapshot",
        expected_any=("=== Config ===", "state=", "mode="),
        validators=("driver_ready", "configured_address"),
        timeout_s=1.0,
    )
    row = hil.run_serial(
        FakeSerial(
            "=== Config ===\n  Initialized: true\n  State: R",
            "EADY\n  I2C address: 0x44\n  Mode: SINGLE_SHOT\n> ",
        ),
        spec,
        idle_s=0.0,
        args=fake_args(),
    )
    assert row["result"] == hil.RESULT_PASS, row["notes"]
    assert row["parsed"]["state"] == "READY"
    assert row["parsed"]["configured_i2c_address"] == "0x44"


def test_health_command_accepts_complete_validators_without_prompt() -> None:
    spec = hil.CommandSpec(
        "drv",
        "health snapshot",
        expected_any=("Driver Health", "state=", "online="),
        validators=("driver_ready", "zero_failures"),
        timeout_s=1.0,
    )
    row = hil.run_serial(
        FakeSerial(
            "=== Driver Health ===\n"
            "  State: READY\n"
            "  Online: yes\n"
            "  Consecutive failures: 0\n"
            "  Total success: 4216\n"
            "  Total failures: 0\n"
            "=== Config ===\n"
            "  State: R"
        ),
        spec,
        idle_s=0.0,
        args=fake_args(),
    )
    assert row["result"] == hil.RESULT_PASS, row["notes"]
    assert row["parsed"]["state"] == "READY"
    assert row["parsed"]["online"] is True
    assert row["parsed"]["total_failures"] == 0


def test_optional_command_i2c_timeout_fails_not_unsupported() -> None:
    spec = hil.CommandSpec(
        "art start",
        "optional art timeout",
        expected_any=("art start: OK",),
        unsupported_ok=True,
        timeout_s=0.1,
    )
    result, notes, _ = classify(spec, "art start: I2C_TIMEOUT\n")
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_summary_markdown_lists_itself_as_artifact() -> None:
    assert any(
        isinstance(value, tuple) and "summary.md" in value and "summary.json" in value
        for value in hil.write_summary_md.__code__.co_consts
    )


def test_append_progress_writes_compact_jsonl() -> None:
    row = hil.result_row(
        hil.version_step(),
        hil.RESULT_PASS,
        "",
        0.123,
        "verbose serial output should stay out of progress",
        "completion-token+idle",
        {"library_version": "1.6.0"},
    )
    with tempfile.TemporaryDirectory() as tmp_dir:
        path = Path(tmp_dir) / "progress.jsonl"
        hil.append_progress(argparse.Namespace(progress_path=str(path)), row)
        payload = json.loads(path.read_text(encoding="utf-8"))
    assert payload["command"] == "version"
    assert payload["result"] == hil.RESULT_PASS
    assert payload["parsed"]["library_version"] == "1.6.0"
    assert "output" not in payload


def test_operator_required_classification() -> None:
    spec = hil.operator_specs()[0]
    parsed = hil.parse_command_output(spec.command, "")
    result, notes = hil.classify("", spec, False, parsed, fake_args())
    assert result == hil.RESULT_OPERATOR
    assert notes == hil.RESULT_OPERATOR


def main() -> int:
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("test_run_i2c_hil_parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
