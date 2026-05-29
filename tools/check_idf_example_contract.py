#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FORBIDDEN_IDF_TOKENS = [
    "Arduino.h",
    "Wire.h",
    "ArduinoCompat",
    "IdfArduinoCompat",
    "TwoWire",
    "String",
    "Serial",
    "examples/01_basic_bringup_cli/main.cpp",
    "setup();",
    "loop();",
]


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

    combined_idf = (
        idf_main.read_text(encoding="utf-8", errors="replace")
        + "\n"
        + idf_transport.read_text(encoding="utf-8", errors="replace")
        + "\n"
        + idf_cmake.read_text(encoding="utf-8", errors="replace")
    )
    for token in FORBIDDEN_IDF_TOKENS:
        if token in {"TwoWire", "String", "Serial", "ArduinoCompat", "IdfArduinoCompat"}:
            found = re.search(rf"\b{re.escape(token)}\b", combined_idf) is not None
        else:
            found = token in combined_idf
        if found:
            fail(f"IDF example uses forbidden Arduino/compat token: {token}")
    if "driver/i2c.h" in combined_idf:
        fail("IDF example must use driver/i2c_master.h, not legacy driver/i2c.h")

    for needle in (
        'extern "C" void app_main(void)',
        "handleCommandLine",
        "char buffer[LINE_LEN]",
        "std::fgets",
        "i2c_master_probe",
        "driver/i2c_master.h",
        "esp_timer_get_time",
        "vTaskDelay",
        "gConfig.nowMs",
        "gConfig.nowUs",
        "gConfig.cooperativeYield",
    ):
        require_text(idf_main, needle)

    if "../../../common/Sht3xCli.cpp" in idf_cmake.read_text(
        encoding="utf-8", errors="replace"
    ):
        fail("IDF example must not compile examples/common/Sht3xCli.cpp")
    for component in ("SHT3x-main", "esp_driver_i2c", "esp_timer", "freertos", "vfs"):
        require_text(idf_cmake, component)
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
