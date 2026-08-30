#!/usr/bin/env python3
"""Keep the core driver framework-neutral.

`include/` and `src/` must not call platform timing primitives or include
Arduino/ESP-IDF/FreeRTOS headers. All timing reaches the driver through the
`Config::nowMs`, `Config::nowUs`, and `Config::cooperativeYield` hooks.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "include")
VALID_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

FORBIDDEN_CALLS = {
    "millis": re.compile(r"\bmillis\s*\("),
    "micros": re.compile(r"\bmicros\s*\("),
    "delayMicroseconds": re.compile(r"\bdelayMicroseconds\s*\("),
    "yield": re.compile(r"\byield\s*\("),
}

FORBIDDEN_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](?:Arduino\.h|Wire\.h|driver/[^>"]+|esp_[^>"]+|freertos/[^>"]+)[>"]',
    re.MULTILINE,
)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def strip_non_code(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return STRING_RE.sub('""', text)


def collect_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for dirname in SCAN_DIRS:
        root = ROOT / dirname
        if not root.exists():
            continue
        files.extend(
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in VALID_SUFFIXES
        )
    return sorted(files)


def main() -> int:
    errors: list[str] = []

    for path in collect_sources():
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")

        if FORBIDDEN_INCLUDE_RE.search(raw) is not None:
            errors.append(f"{rel}: includes an Arduino/ESP-IDF/FreeRTOS header")

        code = strip_non_code(raw)
        for call_name, pattern in FORBIDDEN_CALLS.items():
            if pattern.search(code) is not None:
                errors.append(
                    f"{rel}: calls {call_name}(); use the Config timing hooks instead"
                )

    if errors:
        print("Core timing guard FAILED:")
        for err in errors:
            print(f"- {err}")
        return 1

    print("Core timing guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
