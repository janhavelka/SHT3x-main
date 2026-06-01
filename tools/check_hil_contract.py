#!/usr/bin/env python3
"""Guard the SHT3x serial HIL runner and auditor documentation contract."""

from __future__ import annotations

import importlib.util
import py_compile
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "run_i2c_hil.py"
RUNBOOK = ROOT / "docs" / "SHT3X_I2C_HIL_RUNBOOK.md"
TARGET_TEMPLATE = ROOT / "docs" / "SHT3X_I2C_HIL_TARGET_TEMPLATE.md"
SELFTEST_REPORT = ROOT / "docs" / "SHT3X_I2C_HIL_SELFTEST_REPORT.md"
README = ROOT / "README.md"
DOCS_INDEX = ROOT / "docs" / "README.md"
MATRIX = ROOT / "docs" / "SHT3X_HARDWARE_VALIDATION_MATRIX.md"
GITIGNORE = ROOT / ".gitignore"

DEFAULT_MARKER_RE = re.compile(
    r"<!-- BEGIN DEFAULT_HIL_COMMANDS -->\s*```text\s*(.*?)\s*```\s*<!-- END DEFAULT_HIL_COMMANDS -->",
    re.DOTALL,
)

FORBIDDEN_DEFAULTS = (
    "clear_status",
    "clearstatus",
    "heater on",
    "alert disable",
    "alert set",
    "alert write",
    "alert raw write",
    "reset",
    "defaults",
    "restore",
    "iface_reset",
    "greset",
    "selftest",
    "recover",
    "stress",
    "stress_mix",
    "command write",
    "command write_data",
    "command read",
)

FORBIDDEN_CLAIMS = (
    "hardware validation passed",
    "hardware validated",
    "alert pin validation passed",
    "humidity accuracy validated",
    "full hil pass",
    "production hardware pass",
)


def fail(message: str) -> None:
    raise SystemExit(f"check_hil_contract: {message}")


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        fail(f"missing required file: {path.relative_to(ROOT)}")


def import_runner():
    spec = importlib.util.spec_from_file_location("run_i2c_hil_contract", RUNNER)
    if spec is None or spec.loader is None:
        fail("cannot import tools/run_i2c_hil.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def documented_commands(runbook_text: str) -> list[str]:
    match = DEFAULT_MARKER_RE.search(runbook_text)
    if not match:
        fail("runbook missing DEFAULT_HIL_COMMANDS marker block")
    return [line.strip() for line in match.group(1).splitlines() if line.strip()]


def check_claims() -> None:
    for path in (RUNBOOK, TARGET_TEMPLATE, SELFTEST_REPORT, README, DOCS_INDEX, MATRIX):
        text = read(path).lower()
        for phrase in FORBIDDEN_CLAIMS:
            if phrase in text:
                fail(f"unsupported hardware-pass claim in {path.relative_to(ROOT)}: {phrase}")


def main() -> int:
    for path in (RUNNER, RUNBOOK, TARGET_TEMPLATE, SELFTEST_REPORT, README, DOCS_INDEX, MATRIX, GITIGNORE):
        if not path.exists():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    py_compile.compile(str(RUNNER), doraise=True)
    runner_text = read(RUNNER)
    for token in ("--dry-run", "python -m pip install pyserial", "serial.Serial"):
        if token not in runner_text:
            fail(f"runner missing required token: {token}")
    for token in (
        "--include-clock-stretch",
        "--include-alert-write",
        "--include-all-periodic-rates",
        "--include-bus-wide-reset",
        "--expect-address",
        "environment.txt",
    ):
        if token not in runner_text:
            fail(f"runner missing extended HIL token: {token}")

    runner = import_runner()
    actual = list(runner.default_executable_commands())
    expected = documented_commands(read(RUNBOOK))
    if actual != expected:
        fail(f"default command sequence mismatch: runner={actual!r} docs={expected!r}")

    for command in actual:
        lowered = command.lower()
        for forbidden in FORBIDDEN_DEFAULTS:
            if lowered == forbidden or lowered.startswith(forbidden + " "):
                fail(f"unsafe command in default executable sequence: {command}")

    gitignore_lines = {line.strip() for line in read(GITIGNORE).splitlines()}
    if "hil_logs/" not in gitignore_lines:
        fail(".gitignore must include hil_logs/")

    runbook_text = read(RUNBOOK)
    for token in (
        "ACK alone is not chip identity",
        "OPERATOR_REVIEW_REQUIRED",
        "No physical HIL validation was performed",
        "SKIP_REQUIRES_FIXTURE",
        "SKIP_UNSUPPORTED",
        "summary.json",
        "serial_transcript.txt",
        "environment.txt",
    ):
        if token not in runbook_text:
            fail(f"runbook missing required text: {token}")

    report_text = read(SELFTEST_REPORT)
    if "software-prepared only" not in report_text:
        fail("self-test report must use software-prepared only verdict")

    readme_text = read(README)
    if "docs/README.md" not in readme_text:
        fail("README must point to docs/README.md")
    docs_index_text = read(DOCS_INDEX)
    for path in (RUNBOOK, TARGET_TEMPLATE, SELFTEST_REPORT):
        rel = str(path.relative_to(ROOT)).replace("\\", "/")
        bare = path.name
        if rel not in docs_index_text and bare not in docs_index_text:
            fail(f"docs/README.md missing documentation link: {rel}")

    matrix_text = read(MATRIX)
    if "tools/run_i2c_hil.py" not in matrix_text or "docs/SHT3X_I2C_HIL_RUNBOOK.md" not in matrix_text:
        fail("hardware validation matrix must point to the serial HIL runner contract")

    check_claims()
    print("check_hil_contract: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
