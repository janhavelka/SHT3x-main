#!/usr/bin/env python3
"""Small parser checks for tools/run_i2c_hil.py."""

from __future__ import annotations

import argparse
import importlib.util
import sys
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


def test_unknown_command_fails() -> None:
    spec = hil.CommandSpec("bogus", "test", expected_any=("OK",))
    result, notes, _ = classify(spec, "Unknown command. Try 'help'.\n")
    assert result == hil.RESULT_FAIL
    assert "failure token" in notes


def test_status_output_parses() -> None:
    parsed = hil.parse_command_output(
        "status",
        "status: raw=0x8010 alert=1 heater=0 rh_alert=0 t_alert=0 reset=1 cmd_err=0 crc_err=0\n",
    )
    assert parsed["status_word"] == "0x8010"
    assert parsed["status_bits"]["alert"] == 1
    assert parsed["status_bits"]["reset"] == 1


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
