# SHT3x HIL Operator Checklist

These checks are not proven by the default automated serial smoke run.

| Item | Required opt-in | Evidence needed |
| --- | --- | --- |
| `selftest` | `--include-destructive` | Arduino selftest performs softReset(). |
| `recover` | `--include-destructive` |  |
| `stress 10` | `--include-soak` |  |
| `ALERT threshold procedure` | `--include-output-tests` | Requires GPIO or logic-analyzer evidence. |
| `fault/unplug/CRC-injection procedure` | `--include-fault-tests` | Requires a safe jig or interposer. |
