#!/usr/bin/env python3
"""Validate maintained documentation and reject tracked working artifacts."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
DISALLOWED_PARTS = {".doxygen", ".pio", "__pycache__", "hil_logs", "prompts"}
DISALLOWED_NAMES = {
    "progress.jsonl",
    "serial_transcript.txt",
    "operator_checklist.md",
    "operator_notes.md",
    "summary.json",
    "summary.md",
}
DISALLOWED_SUFFIXES = {".bak", ".log", ".orig", ".rej", ".tmp"}
DOXYGEN_REQUIREMENTS = {
    "EXTRACT_ALL": "NO",
    "WARN_IF_UNDOCUMENTED": "YES",
    "WARN_IF_INCOMPLETE_DOC": "YES",
    "WARN_NO_PARAMDOC": "YES",
    "WARN_IF_DOC_ERROR": "YES",
    "WARN_AS_ERROR": "FAIL_ON_WARNINGS",
}


def maintained_files() -> list[Path]:
    completed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [Path(item.decode("utf-8")) for item in completed.stdout.split(b"\0") if item]


def check_leftovers(paths: list[Path]) -> list[str]:
    errors: list[str] = []
    for path in paths:
        lowered_parts = {part.lower() for part in path.parts}
        lowered_name = path.name.lower()
        if lowered_parts.intersection(DISALLOWED_PARTS):
            errors.append(f"working-artifact path is not ignored: {path.as_posix()}")
        elif lowered_name in DISALLOWED_NAMES:
            errors.append(f"generated HIL artifact is not ignored: {path.as_posix()}")
        elif path.suffix.lower() in DISALLOWED_SUFFIXES:
            errors.append(f"temporary artifact is not ignored: {path.as_posix()}")
    return errors


def check_markdown_links(paths: list[Path]) -> list[str]:
    errors: list[str] = []
    for relative in paths:
        if relative.suffix.lower() != ".md":
            continue
        source = ROOT / relative
        text = source.read_text(encoding="utf-8")
        for match in MARKDOWN_LINK_RE.finditer(text):
            target = match.group(1).strip().strip("<>")
            if not target or target.startswith(("#", "http://", "https://", "mailto:")):
                continue
            path_text = target.split("#", 1)[0]
            if not path_text:
                continue
            resolved = (source.parent / path_text).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                errors.append(f"{relative.as_posix()}: link escapes repository: {target}")
                continue
            if not resolved.exists():
                errors.append(f"{relative.as_posix()}: broken local link: {target}")
    return errors


def check_doxygen() -> list[str]:
    text = (ROOT / "Doxyfile").read_text(encoding="utf-8")
    errors: list[str] = []
    for key, expected in DOXYGEN_REQUIREMENTS.items():
        match = re.search(rf"^{re.escape(key)}\s*=\s*(\S+)\s*$", text, re.MULTILINE)
        actual = match.group(1) if match else "<missing>"
        if actual != expected:
            errors.append(f"Doxyfile {key}: expected {expected}, found {actual}")
    return errors


def main() -> int:
    try:
        paths = maintained_files()
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"check_docs_contract: cannot enumerate maintained files: {exc}", file=sys.stderr)
        return 2

    errors = check_leftovers(paths)
    errors.extend(check_markdown_links(paths))
    errors.extend(check_doxygen())
    if errors:
        for error in errors:
            print(f"check_docs_contract: {error}", file=sys.stderr)
        return 1

    markdown_count = sum(path.suffix.lower() == ".md" for path in paths)
    print(f"Documentation contract PASSED ({markdown_count} maintained Markdown files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
