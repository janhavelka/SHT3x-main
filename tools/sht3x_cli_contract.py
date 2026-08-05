#!/usr/bin/env python3
"""Authoritative host-side contract for both SHT3x diagnostic CLIs.

The Arduino and native ESP-IDF examples intentionally retain independent
framework-specific implementations.  This module is not compiled into either
firmware; repository checks compare both implementations with these ordered
command rows so help, safety gates, and execution ownership cannot drift.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterable


EXECUTION_CLASSES = (
    "CACHE_ONLY",
    "PURE",
    "CORE_JOB",
    "BOUNDED_SYNC",
    "CLI_JOB",
    "RAW_I2C",
    "LIFECYCLE",
)

SAFETY_CLASSES = (
    "SAFE",
    "CONFIRM_MUTATION",
    "ARM_CONFIRM_BUS_WIDE",
)


@dataclass(frozen=True)
class CommandSpec:
    command_id: str
    canonical: str
    aliases: tuple[str, ...]
    synopsis: str
    description: str
    execution: str
    safety: str = "SAFE"


def _spec(
    command_id: str,
    canonical: str,
    aliases: tuple[str, ...],
    synopsis: str,
    description: str,
    execution: str,
    safety: str = "SAFE",
) -> CommandSpec:
    return CommandSpec(
        command_id,
        canonical,
        aliases,
        synopsis,
        description,
        execution,
        safety,
    )


COMMAND_SPECS = (
    _spec("HELP", "help", ("?",), "help / ?", "Show this help", "CACHE_ONLY"),
    _spec("VERSION", "version", ("ver",), "version / ver", "Print runtime framework and firmware/library provenance", "CACHE_ONLY"),
    _spec("SCAN", "scan", (), "scan", "Scan I2C ACKs; an ACK is not SHT3x identity", "RAW_I2C"),
    _spec("READ", "read", (), "read", "Run one bounded owner-safe measurement job", "CORE_JOB"),
    _spec("REQUEST", "request", (), "request", "Schedule an owner-safe measurement without I2C", "CORE_JOB"),
    _spec("FETCH", "fetch", (), "fetch", "Advance the active owner job by at most one I2C transfer", "CORE_JOB"),
    _spec("RAW", "raw", (), "raw", "Print the last cached raw sample", "CACHE_ONLY"),
    _spec("COMP", "comp", (), "comp", "Print the last cached compensated sample", "CACHE_ONLY"),
    _spec("MEAS_TIME", "meastime", (), "meastime", "Show estimated measurement time", "PURE"),
    _spec("JOB", "job", (), "job [current|last]", "Show active zero-I2C progress or the retained terminal result", "CACHE_ONLY"),
    _spec("JOB_STEP", "job", (), "job step <0..255>", "Poll once with an explicit I2C-transfer budget", "CORE_JOB"),
    _spec("CANCEL", "cancel", (), "job cancel / cancel", "Cancel the active job locally with zero I2C", "CACHE_ONLY"),
    _spec("RESULT", "result", (), "result", "Show the retained terminal job result", "CACHE_ONLY"),
    _spec("MODE", "mode", (), "mode [single|periodic|art]", "Set or show operating mode", "BOUNDED_SYNC"),
    _spec("SINGLE", "single", (), "single <low|medium|high>", "Run one no-stretch single-shot measurement", "CORE_JOB"),
    _spec("PERIODIC_START", "periodic", (), "periodic start <rate> <rep>", "Start periodic mode", "BOUNDED_SYNC"),
    _spec("PERIODIC_FETCH", "periodic", (), "periodic fetch", "Run one bounded periodic fetch job", "CORE_JOB"),
    _spec("PERIODIC_STOP", "periodic", (), "periodic stop", "Stop periodic or ART mode", "BOUNDED_SYNC"),
    _spec("ART_START", "art", (), "art start", "Start ART mode", "BOUNDED_SYNC"),
    _spec("ART_FETCH", "art", (), "art fetch", "Run one bounded ART fetch job", "CORE_JOB"),
    _spec("ART_STOP", "art", (), "art stop", "Stop ART mode", "BOUNDED_SYNC"),
    _spec("START_PERIODIC", "start_periodic", (), "start_periodic <rate> <rep>", "Alias for periodic start", "BOUNDED_SYNC"),
    _spec("START_ART", "start_art", (), "start_art", "Alias for art start", "BOUNDED_SYNC"),
    _spec("STOP_PERIODIC", "stop_periodic", (), "stop_periodic", "Alias for periodic/art stop", "BOUNDED_SYNC"),
    _spec("REPEAT", "repeat", (), "repeat [low|med|high]", "Set or show repeatability", "BOUNDED_SYNC"),
    _spec("RATE", "rate", (), "rate [0.5|1|2|4|10]", "Set or show periodic rate", "BOUNDED_SYNC"),
    _spec("STRETCH", "stretch", (), "stretch [0|1]", "Set or show clock stretching", "CACHE_ONLY"),
    _spec("STATUS", "status", (), "status", "Read decoded status while idle", "BOUNDED_SYNC"),
    _spec("STATUS_RESTORE", "status_restore", (), "status_restore confirm", "Interrupt active acquisition, read status, and restore mode", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("STATUS_RAW", "status_raw", (), "status_raw", "Read raw status while idle", "BOUNDED_SYNC"),
    _spec("CLEAR_STATUS", "clearstatus", ("clear_status",), "clearstatus confirm", "Clear sticky status flags", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("CLEAR_STATUS_ALIAS", "clear_status", (), "clear_status confirm", "Alias for clearstatus", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("HEATER", "heater", (), "heater [status|off|on confirm]", "Inspect or control the heater; enabling requires confirmation", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("SERIAL", "serial", (), "serial [stretch|nostretch]", "Read the CRC-protected serial number", "BOUNDED_SYNC"),
    _spec("COMMAND_WRITE", "command", (), "command write <hex> confirm", "Issue an arbitrary raw 16-bit command", "RAW_I2C", "CONFIRM_MUTATION"),
    _spec("COMMAND_WRITE_DATA", "command", (), "command write_data <cmd> <data> confirm", "Issue an arbitrary command with packed data", "RAW_I2C", "CONFIRM_MUTATION"),
    _spec("COMMAND_READ", "command", (), "command read <cmd> <len> confirm", "Issue an arbitrary command and raw read", "RAW_I2C", "CONFIRM_MUTATION"),
    _spec("ALERT_SHOW", "alert", (), "alert show", "Read all alert limits", "BOUNDED_SYNC"),
    _spec("ALERT_SET", "alert", (), "alert set <kind> <T> <RH> confirm", "Write an alert limit", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("ALERT_READ", "alert", (), "alert read <hs|hc|lc|ls>", "Read an alert limit", "BOUNDED_SYNC"),
    _spec("ALERT_WRITE", "alert", (), "alert write <kind> <T> <RH> confirm", "Alias for alert set", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("ALERT_RAW_READ", "alert", (), "alert raw read <kind>", "Read a raw packed alert word", "BOUNDED_SYNC"),
    _spec("ALERT_RAW_WRITE", "alert", (), "alert raw write <kind> <hex> confirm", "Write a raw packed alert word", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("ALERT_ENCODE", "alert", (), "alert encode <T> <RH>", "Encode an alert word without I2C", "PURE"),
    _spec("ALERT_DECODE", "alert", (), "alert decode <hex>", "Decode an alert word without I2C", "PURE"),
    _spec("ALERT_DISABLE", "alert", (), "alert disable confirm", "Disable alerts by writing limits", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("CONVERT", "convert", (), "convert <rawT> <rawRH>", "Convert measurement words without I2C", "PURE"),
    _spec("RESET", "reset", (), "reset confirm", "Soft-reset the sensor", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("DEFAULTS", "defaults", (), "defaults confirm", "Reset command-mode defaults", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("RESTORE", "restore", (), "restore confirm", "Reset the sensor and restore cached settings", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("IFACE_RESET", "iface_reset", (), "iface_reset confirm", "Run the injected interface-reset callback", "BOUNDED_SYNC", "CONFIRM_MUTATION"),
    _spec("GRESET_ARM", "greset", (), "greset arm / greset disarm", "Arm or disarm one bus-wide general-call reset; no I2C", "CACHE_ONLY", "ARM_CONFIRM_BUS_WIDE"),
    _spec("GRESET_CONFIRM", "greset", (), "greset confirm", "Attempt one armed bus-wide reset when application transport enables it", "RAW_I2C", "ARM_CONFIRM_BUS_WIDE"),
    _spec("STATS", "stats", (), "stats", "Show runtime counters and cached settings", "CACHE_ONLY"),
    _spec("CFG", "cfg", ("settings",), "cfg / settings", "Show current config; settings also reads status", "BOUNDED_SYNC"),
    _spec("DRIVER", "drv", (), "drv", "Show driver state and health", "CACHE_ONLY"),
    _spec("ONLINE", "online", (), "online", "Show online state", "CACHE_ONLY"),
    _spec("BEGIN", "begin", (), "begin", "Owner-safe bind and bounded ensure-idle reconciliation", "LIFECYCLE"),
    _spec("END", "end", (), "end", "End the local driver session; rejects an active job", "LIFECYCLE"),
    _spec("STATE", "state", (), "state", "Show compact one-line health summary", "CACHE_ONLY"),
    _spec("PROBE", "probe", (), "probe", "Probe the sensor without health tracking", "BOUNDED_SYNC"),
    _spec("RECOVER", "recover", (), "recover confirm", "Run bounded owner-safe ensure-idle recovery", "CORE_JOB", "CONFIRM_MUTATION"),
    _spec("VERBOSE", "verbose", (), "verbose [0|1]", "Show or set verbose output", "CACHE_ONLY"),
    _spec("STRESS", "stress", (), "stress [N]", "Run N bounded measurement jobs", "CLI_JOB"),
    _spec("STRESS_MIX", "stress_mix", (), "stress_mix [N]", "Run N mixed operations", "CLI_JOB"),
    _spec("I2C_SOAK", "i2c_soak", (), "i2c_soak <seconds>", "Run a bounded low-output measurement soak", "CLI_JOB"),
    _spec("XFER_RESET", "xfer_reset", (), "xfer_reset", "Reset example-owned transport counters", "CACHE_ONLY"),
    _spec("XFER_STATS", "xfer_stats", (), "xfer_stats", "Show example-owned transport counters", "CACHE_ONLY"),
    _spec("XFER_ASSERT", "xfer_assert", (), "xfer_assert <read> <write> <total>", "Assert exact transport callback totals", "CACHE_ONLY"),
    _spec("SELFTEST", "selftest", (), "selftest confirm", "Run diagnostic I2C self-test commands", "CLI_JOB", "CONFIRM_MUTATION"),
)


HELP_ROW_RE = re.compile(
    r'(?:\bcli::)?printHelpItem\(\s*"(?P<synopsis>(?:\\.|[^"\\])*)"\s*,\s*'
    r'"(?P<description>(?:\\.|[^"\\])*)"\s*\)',
    re.DOTALL,
)


def _decode_cpp_string(value: str) -> str:
    return (
        value.replace(r"\n", "\n")
        .replace(r"\t", "\t")
        .replace(r'\"', '"')
        .replace(r"\\", "\\")
    )


def parse_help_rows(source: str) -> tuple[tuple[str, str], ...]:
    """Extract deliberately literal firmware help rows in source order."""
    return tuple(
        (
            _decode_cpp_string(match.group("synopsis")),
            _decode_cpp_string(match.group("description")),
        )
        for match in HELP_ROW_RE.finditer(source)
    )


def expected_help_rows() -> tuple[tuple[str, str], ...]:
    return tuple((spec.synopsis, spec.description) for spec in COMMAND_SPECS)


def command_names() -> frozenset[str]:
    names: set[str] = set()
    for spec in COMMAND_SPECS:
        names.add(spec.canonical)
        names.update(spec.aliases)
    return frozenset(names)


def validate_contract(specs: Iterable[CommandSpec] = COMMAND_SPECS) -> list[str]:
    errors: list[str] = []
    ids: set[str] = set()
    synopses: set[str] = set()
    for spec in specs:
        if spec.command_id in ids:
            errors.append(f"duplicate command id: {spec.command_id}")
        ids.add(spec.command_id)
        if spec.synopsis in synopses:
            errors.append(f"duplicate help synopsis: {spec.synopsis}")
        synopses.add(spec.synopsis)
        if spec.execution not in EXECUTION_CLASSES:
            errors.append(f"{spec.command_id}: invalid execution class {spec.execution}")
        if spec.safety not in SAFETY_CLASSES:
            errors.append(f"{spec.command_id}: invalid safety class {spec.safety}")
        if spec.safety == "CONFIRM_MUTATION" and "confirm" not in spec.synopsis:
            errors.append(f"{spec.command_id}: confirmed mutation lacks confirm syntax")
        if spec.safety == "CONFIRM_MUTATION" and spec.execution in ("CACHE_ONLY", "PURE"):
            errors.append(
                f"{spec.command_id}: confirmed hardware mutation cannot be {spec.execution}"
            )

    greset_rows = [spec.synopsis for spec in specs if spec.command_id.startswith("GRESET_")]
    if greset_rows != ["greset arm / greset disarm", "greset confirm"]:
        errors.append("general-call reset must retain the arm/disarm then confirm contract")
    return errors
