# Code Audit Remediation Report

This report records the disposition of every finding in
[CODE_AUDIT.md](CODE_AUDIT.md) against the remediated working tree. `Valid`
means the underlying issue was confirmed; `partial` means only part of the
premise or proposal held; `rejected` means the current contract is intentional;
and `resolved` means the accepted part was implemented or documented.

The review covered the framework-neutral core, public API, native tests,
Arduino and ESP-IDF diagnostic adapters, shared CLI, repository guards, and
package manifests. No new hardware, ALERT-pin, sensor-accuracy, soak, or board
validation is claimed by this remediation.

A second, fresh review then re-read the original audit and inspected the
completed implementation and diff without relying on the first summary. Three
parallel reviews covered findings 1-7, findings 8-18, and findings 19-22 plus
scope and simplicity. Their claims were reproduced against the code before
acceptance. No additional production-driver bug was confirmed. The follow-up
did identify missing direct regressions, one incomplete partial-state API note,
a tautological scanner stub, duplicated contract checks, and obsolete HIL
output alternatives; all are addressed below.

## Findings 1-22

| # | Verdict | Resolution and evidence |
| --- | --- | --- |
| 1 | Valid; resolved with a refined proposal | Periodic Fetch Data now tolerates either a proven read-header NACK or, when the adapter cannot prove one, a generic read error inferred as no-data only inside the driver's cadence-derived window. Proven and inferred observations have separate saturating counters and neither increments transport failures. This preserves honest Wire capability reporting without treating an indefinitely absent sensor as healthy. |
| 2 | Valid; resolved | `notReadyTimeoutMs == 0` now derives a finite window of three acquisition periods plus the fetch margin. Expiry terminates the job with one logical `TIMEOUT`; no cooperative job can retry no-data forever merely because the caller omitted a deadline. |
| 3 | Valid; resolved | The no-data streak timestamp/count are cleared on terminal job cleanup, timeout, successful fetch, and acquisition reset. A later job can establish a fresh bounded window. |
| 4 | Rejected; no change | The proposal conflicts with the repository health contract: `INVALID_CONFIG`, `INVALID_PARAM`, and `IN_PROGRESS` are not hardware-health failures. Adapters must return the documented transport status family after an admitted callback; out-of-contract configuration/parameter statuses remain health-neutral rather than being reclassified as bus failures. Admission checks were kept centralized without changing that rule. |
| 5 | Valid; resolved with a simpler rule | `begin()` attempts Break and soft reset independently. It succeeds only when either command was accepted and its required settle wait completed, followed by a clean CRC-valid status read. Requiring both commands would reject a valid baseline after one successful reconciliation path; status alone still proves only communication. The bound is four callbacks, or five with the optional periodic/ART start. |
| 6 | Valid; resolved with exact-once accounting | Logical completion was moved after CRC/status validation instead of first recording transport success and then trying to undo it. A CRC/checksum or sensor command-rejection result is exactly one logical failure, participates in `DEGRADED`/`OFFLINE`, increments `protocolFailures()`, and does not increment `transportFailures()`. |
| 7 | Rejected; documented | The Arduino adapter's `timeoutMs` is a hard callback-completion bound. A callback that returns after it is a timeout even if Wire also reports complete data; late read bytes are drained. Accepting a late completion would conceal a violated deterministic owner bound. Existing timeout tests and policy were retained. |
| 8 | Valid; resolved | `pollJob()` performs one deadline admission check before work. A callback admitted before expiry may finish after it and its validated result is retained; if more work remains, the next poll times out without starting another callback. Completed samples and verified ensure-idle results are no longer discarded by post-work checks. |
| 9 | Valid; resolved | Cancelling a measurement whose effect is only `RESULT_MAY_BE_PENDING` preserves an established acquisition baseline. Only an indeterminate device-state effect, or a partially applied ensure-idle job, invalidates that proof. Cancellation remains local and zero-I2C. |
| 10 | Valid; resolved | Recovery backoff is now caller-side admission. A rejected attempt returns `BUSY` before I2C, does not invalidate verified state or update health, and does not refresh the last-attempt timestamp. |
| 11 | Rejected; documented | The vendor tIDLE requirement is command-to-next-command. A receive-only response header is not a command, so its completion does not restart tIDLE. Command attempts remain stamped even on callback failure, and command-response reads still wait after their command. The separate microsecond-wrap weakness was fixed under finding 18.2. |
| 12 | Valid; resolved | A no-data retry now waits the periodic fetch margin, clamped to at least `commandDelayMs`, rather than waiting a whole acquisition period and predictably losing an output. The bounded window from findings 1-3 limits retries when the sensor is not producing. |
| 13 | Valid; resolved | Successful-fetch accounting carries sub-period remainder, rejects backward/discontinuous timestamps, and saturates. `missedSamplesEstimate()` is explicitly an estimate of sensor outputs not fetched, including loss caused by the owner's own slower polling cadence. |
| 14 | Valid; resolved | ART's fixed vendor command carries neither periodic rate nor repeatability. Both setters therefore update only the desired restore cache in ART mode and perform zero I2C; active `PERIODIC` mode still uses a checked restart. |
| 15 | Valid; resolved | `probe()` returns `BUSY` during periodic or ART acquisition instead of issuing Read Status in a mode where only Fetch Data is documented as the acquisition readout. It remains raw and health-neutral when admitted. |
| 16 | Valid adapter limitation; proposed mapping rejected | The native scanner uses `i2c_master_probe()` and can report exact address absence. The driver's injected adapter uses `i2c_master_transmit()`/`i2c_master_receive()`, whose supported return contract does not establish address-, data-, or read-header-NACK phase; `ESP_ERR_INVALID_RESPONSE` therefore remains generic `I2C_ERROR`. Mapping `ESP_ERR_NOT_FOUND` in callbacks that do not promise to return it would fabricate provenance, so driver-level `probe()` cannot guarantee `DEVICE_NOT_FOUND` with this adapter. |
| 17 | Valid; resolved | `setHeater()` now performs the heater command plus a status command/read, rejects status diagnostics or a heater-bit mismatch, and updates the cache only after verification. The synchronous bound is three callbacks. |
| 18 | Mixed; itemized below | All twelve smaller items were reviewed individually; accepted items were fixed or documented without introducing parallel abstractions. |
| 19 | Partial; targeted gaps resolved | Test isolation now resets global clocks, Wire state, and transfer counters. Focused coverage was added for OFFLINE escape, multiple-fetch/remainder missed-output accounting, serial assembly and both CRCs, alert reads/CRC, stronger sample-age and permanent-OFFLINE assertions, and a heater-aware status mock. The fresh review also added exact retry-delay, full-wrap timing, deadline-admission, zero-I2C active-mode probe, public alert-limit readback, and heater-status regressions. The scanner stub now ACKs only a selected address, so its timeout-restoration test is no longer tautological. The mocks remain intentionally bounded host doubles rather than a full speculative sensor simulator. |
| 20 | Valid; resolved | Arduino and native ESP-IDF now compile one framework-neutral fixed-buffer `Sht3xCli` command processor. Platform-specific output, time/yield, scan, transfer statistics, I2C ownership, and task setup remain injected example hooks; the IDF build uses no Arduino facade. Contract guards enforce sharing and native boundaries without duplicating the shared CLI checks, and the HIL runner accepts the unified CLI's current output rather than retaining obsolete implementation variants. |
| 21 | Valid; resolved | PlatformIO's optional `frameworks` and `platforms` allow-lists and ESP-IDF's optional `targets` restriction were removed, and package descriptions are framework-neutral. Existing export include/exclude policy and packaged HIL runner/contract were intentionally retained and archive-checked. S2/S3 remain validation targets, not compatibility limits. |
| 22 | Rejected as a current defect; release state verified | At review time, both local and remote `v1.8.0` resolve to annotated tag object `4b177cff998eaaa8502e50559a90cdc4d0be80c5`, which dereferences to release commit `524850da66077a018a921949eb429b22b76858c3`; that commit is an ancestor of the remediation branch. No retag, tag move, or version change was made. |

## Finding 18 Detail

| Item | Verdict | Resolution |
| --- | --- | --- |
| 18.1 Duration helper and mixed clocks | Partially valid; resolved | The problematic no-data comparison now samples both endpoints from the injected internal `nowMs` source. Existing duration gates use same-source unsigned clocks and keep their bounded wrap semantics; deleting the helper wholesale was unnecessary. |
| 18.2 Microsecond tIDLE wrap | Valid; resolved | Command attempts now record both microseconds and milliseconds. The millisecond companion proves a long idle interval across the 71.6-minute microsecond wrap; microseconds retain short-interval precision. |
| 18.3 Measurement-time quantization | Valid; resolved | `estimateMeasurementTimeMs()` adds an unconditional 1 ms allowance for millisecond timestamp truncation, then adds the configured safety margin as separate headroom. |
| 18.4 `readSettings()` BUSY decoding | Valid; resolved | Snapshot-only OK behavior is selected positively from local active-job/periodic state. A transport-returned `BUSY` is preserved instead of being mistaken for a local snapshot condition. |
| 18.5 `resetToDefaults()` physical state | Valid; resolved | After successful recovery, the API always issues and settles a physical soft reset before committing local defaults. Its bound is the recovery bound plus one reset callback and reset wait. The public API and README now also state that a failure after an admitted attempt begins may leave partially changed physical state while the local cache remains uncommitted and `hardwareStateValid()` is false; precondition and backoff rejections preserve state. |
| 18.6 Tracked protocol recording | Valid; resolved | Protocol recording takes an explicit `tracked` argument and is a no-op for untracked diagnostics, keeping raw `probe()` counter-neutral and preventing future call-site drift. |
| 18.7 `PollJobResult::phase` | Rejected; documented | The established contract reports the phase that produced the current result, not the phase queued next. The public field comment now states that semantic; changing it would make existing owner telemetry ambiguous. |
| 18.8 Premature sample-ready clear | Valid; resolved | `requestMeasurement()` now clears the ready flag only after mode-specific validation succeeds and a job is actually scheduled. Rejected requests preserve an unread sample. |
| 18.9 ESP-IDF partial console input | Valid; resolved | The input task accumulates bounded chunks until CR/LF, clears nonblocking stdio error state, supports backspace, discards an entire overlong line, and uses a bounded queue-send timeout. No UART-driver assumption or heap-heavy console layer was added. |
| 18.10 Diagnostic internal pull-ups | Valid; documented | Both diagnostic examples explicitly warn that internal pull-ups are only for bring-up and external pull-ups sized for voltage and capacitance are required for reliable 400 kHz operation. |
| 18.11 Capability-bit validation | Valid; resolved | `bind()` rejects bits outside the defined `TransportCapability` mask before any I2C. |
| 18.12 Dead bare `greset` branch | Valid; resolved | Unreachable bare-command dispatch was removed. Only the explicit `arm`, `disarm`, and `confirm` forms remain in the shared CLI. |

## Validation Record

The integration working tree produced these software results:

- Native PlatformIO tests: 129/129 passed.
- ESP32-S3 and ESP32-S2 Arduino diagnostic examples: both compiled and linked
  successfully with the pinned pioarduino `55.03.311` platform.
- Strict C++17 host compilation of both the core and the shared CLI: passed
  with `-Wall -Wextra -Wpedantic -Werror`.
- Core timing guard: passed.
- Shared CLI contract: passed with 70 authoritative help rows.
- Native ESP-IDF example contract: passed.
- HIL contract and host parser tests: passed.
- Strict Doxygen generation: passed with warnings treated as errors.
- Changed-document local-link check, report coverage check, whitespace check,
  and generated-version synchronization: passed.
- PlatformIO package creation and archive inspection: passed with 32 files;
  both HIL runtime tools were present and excluded test/build/generated paths
  were absent.
- Annotated-tag dereference and ancestor checks: passed as described in
  finding 22.
- Full documentation contract after staging the audit relocation and repairing
  its repository-relative links: passed for all 13 maintained Markdown files.

Native ESP-IDF compilation was not run because `idf.py` is not installed.
Hardware validation remains the separate process documented in
[hardware.md](hardware.md); no hardware result is inferred from the successful
software builds.
