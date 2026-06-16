#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALID_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
VALID_IDF_SUFFIXES = VALID_SOURCE_SUFFIXES | {".txt"}
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
CORE_FORBIDDEN_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](?:Arduino\.h|Wire\.h|driver/[^>"]+|esp_[^>"]+|freertos/[^>"]+)[>"]',
    re.MULTILINE,
)
CORE_FORBIDDEN_CODE_TOKENS = [
    "TwoWire",
    "String",
    "Serial",
    "ArduinoCompat",
    "IdfArduinoCompat",
    "ESP_LOG",
    "i2c_master_",
    "vTaskDelay",
    "xTaskCreate",
]
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def require_text(path: pathlib.Path, needle: str) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    if needle not in text:
        fail(f"missing '{needle}' in {path.as_posix()}")


def strip_non_code(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return STRING_RE.sub('""', text)


def collect_files(root: pathlib.Path, suffixes: set[str]) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in suffixes:
            files.append(path)
    return files


def check_core_boundary() -> None:
    for dirname in ("include/SHT3x", "src"):
        root = ROOT / dirname
        if not root.exists():
            fail(f"missing core directory: {root.as_posix()}")
        for path in collect_files(root, VALID_SOURCE_SUFFIXES):
            rel = path.relative_to(ROOT).as_posix()
            raw = path.read_text(encoding="utf-8", errors="replace")
            if CORE_FORBIDDEN_INCLUDE_RE.search(raw) is not None:
                fail(f"core file includes Arduino/ESP-IDF framework header: {rel}")
            code = strip_non_code(raw)
            for token in CORE_FORBIDDEN_CODE_TOKENS:
                if token.endswith("_"):
                    found = token in code
                else:
                    found = re.search(rf"\b{re.escape(token)}\b", code) is not None
                if found:
                    fail(f"core file uses framework token '{token}': {rel}")


def main() -> int:
    shared_cli = ROOT / "examples" / "common" / "Sht3xCli.cpp"
    idf_main = ROOT / "examples" / "idf" / "basic" / "main" / "main.cpp"
    idf_project = ROOT / "examples" / "idf" / "basic" / "CMakeLists.txt"
    idf_cmake = ROOT / "examples" / "idf" / "basic" / "main" / "CMakeLists.txt"
    idf_transport = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.cpp"
    idf_transport_h = ROOT / "examples" / "idf" / "basic" / "main" / "IdfI2cTransport.h"
    root_cmake = ROOT / "CMakeLists.txt"
    idf_manifest = ROOT / "idf_component.yml"

    for path in (
        shared_cli,
        idf_main,
        idf_project,
        idf_cmake,
        idf_transport,
        idf_transport_h,
        root_cmake,
        idf_manifest,
    ):
        if not path.exists():
            fail(f"missing required file: {path.as_posix()}")

    check_core_boundary()

    idf_files = [
        idf_project,
        idf_cmake,
        *collect_files(ROOT / "examples" / "idf" / "basic" / "main", VALID_IDF_SUFFIXES),
    ]
    combined_idf = "\n".join(
        path.read_text(encoding="utf-8", errors="replace") for path in idf_files
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
    if "SHT3x-main" in combined_idf:
        fail("IDF example must not depend on a checkout-directory component name")

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
    for component in ("esp_driver_i2c", "esp_driver_gpio", "esp_timer", "freertos", "vfs"):
        require_text(idf_cmake, component)
    require_text(idf_cmake, '"../../../../include"')
    require_text(idf_transport, "i2c_master_transmit")
    require_text(idf_transport, "i2c_master_receive")
    require_text(idf_project, 'set(EXTRA_COMPONENT_DIRS "../../../")')
    require_text(idf_project, "project(sht3x_idf_basic)")
    require_text(root_cmake, "idf_component_register")
    require_text(root_cmake, 'SRCS "src/SHT3x.cpp"')
    require_text(root_cmake, 'INCLUDE_DIRS "include"')
    require_text(idf_manifest, "esp32s2")
    require_text(idf_manifest, "esp32s3")
    require_text(idf_manifest, 'idf: ">=5.4"')

    for command in (
        "command read <cmd> <len>",
        "alert raw write <kind> <hex>",
        "stress_mix [N]",
        "selftest",
        "probe",
        "recover",
        "status_restore",
        "periodic start <rate> <rep>",
        "art fetch",
        "alert show",
    ):
        require_text(shared_cli, command)
        require_text(idf_main, command)

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
