#!/usr/bin/env python3
"""Compile actual CLI percentage expressions against 32-bit counter boundaries."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] /
                        'examples/common/Sht3xCli.cpp')
    args = parser.parse_args()
    source = args.source.read_text(encoding='utf-8')
    view = source.split('void printHealthView(', 1)[1].split('  const uint', 1)[1]
    view = '  const uint' + view.split('\n  output.printf(', 1)[0]
    driver = source.split('void printDriverHealth()', 1)[1].split('  const uint32_t totalOk', 1)[1]
    driver = '  const uint32_t totalOk' + driver.split('  const SHT3x::Status lastErr', 1)[0]
    cpp = '''#include <cstdint>
#include <initializer_list>
#include <cmath>
#include <cstdio>
struct Snapshot { uint32_t totalSuccess; uint32_t totalFailures; };
struct Driver {
  uint32_t ok, fail;
  uint32_t totalSuccess() const { return ok; }
  uint32_t totalFailures() const { return fail; }
};
float viewRate(uint32_t ok, uint32_t fail) {
  Snapshot snap{ok, fail};
''' + view + '''
  return pct;
}
float driverRate(uint32_t ok, uint32_t fail) {
  Driver deviceInstance{ok, fail};
''' + driver + '''
  return successRate;
}
int main() {
  const uint32_t cases[][2] = {{0,0}, {1,0}, {0,1}, {UINT32_MAX,0},
      {0,UINT32_MAX}, {UINT32_MAX,1}, {1,UINT32_MAX},
      {UINT32_MAX,UINT32_MAX}, {0x80000000u,0x80000000u}, {1234567,7654321}};
  unsigned failed = 0;
  for (const auto& values : cases) {
    const double sum = static_cast<double>(values[0]) + values[1];
    const double expected = sum ? 100.0 * values[0] / sum : 0.0;
    for (auto rate : {viewRate, driverRate}) {
      const float actual = rate(values[0], values[1]);
      const bool pass = std::isfinite(actual) && actual >= 0.0f && actual <= 100.0f &&
                        (values[0] == 0 || actual > 0.0f) &&
                        std::fabs(actual - expected) < 0.0001;
      std::printf("ok=%u fail=%u expected=%.6f actual=%.6f %s\\n",
          values[0], values[1], expected, actual, pass ? "PASS" : "FAIL");
      failed += !pass;
    }
  }
  std::printf("20 cases, %u failures\\n", failed);
  return failed ? 1 : 0;
}
'''
    compiler = shutil.which(os.environ.get('CXX', 'g++'))
    if not compiler:
        raise RuntimeError('A native C++ compiler is required (CXX or g++ on PATH)')
    with tempfile.TemporaryDirectory(prefix='sht-cli-rate-') as folder:
        folder = Path(folder)
        path = folder / 'health_rate.cpp'
        exe = folder / ('health_rate.exe' if os.name == 'nt' else 'health_rate')
        path.write_text(cpp, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path),
                        '-o', str(exe)], check=True, timeout=30)
        return subprocess.run([str(exe)], timeout=10).returncode


if __name__ == '__main__':
    raise SystemExit(main())
