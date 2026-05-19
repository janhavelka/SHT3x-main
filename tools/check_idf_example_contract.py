#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def require_text(path: pathlib.Path, needle: str) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    if needle not in text:
        fail(f"missing '{needle}' in {path.as_posix()}")


def main() -> int:
    shared_cli = ROOT / "examples" / "common" / "Sht3xCli.cpp"
    idf_main = ROOT / "examples" / "idf" / "basic" / "main" / "main.cpp"
    idf_cmake = ROOT / "examples" / "idf" / "basic" / "main" / "CMakeLists.txt"
    idf_transport = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.cpp"

    for path in (shared_cli, idf_main, idf_cmake, idf_transport):
        if not path.exists():
            fail(f"missing required file: {path.as_posix()}")

    for needle in (
        "Sht3xCli.h",
        "sht3x_cli::processCommand",
        "sht3x_cli::tick",
        "sht3x_cli::printPrompt",
        "i2c_master_probe",
        "driver/i2c_master.h",
    ):
        require_text(idf_main, needle)

    require_text(idf_cmake, "../../../common/Sht3xCli.cpp")
    require_text(idf_transport, "i2c_master_transmit")
    require_text(idf_transport, "i2c_master_receive")

    for command in (
        "command read <cmd> <len>",
        "alert raw write <kind> <hex>",
        "stress_mix [N]",
        "selftest",
        "probe",
        "recover",
    ):
        require_text(shared_cli, command)

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
