# TunnelMonitor-node Integration

Last updated: 2026-08-05

This guide describes the current integration boundary between SHT3x `1.9.0`
and TunnelMonitor-node. The local sibling checkout still exact-pins the
annotated `v1.8.0` release and contains a private `Sht3xModule` plus a product-owned
environment composition for `0x44` and `0x45`. This guide changes no
TunnelMonitor-node source and does not treat its dirty working tree or unrun
physical HIL as validated evidence.

## Ownership In Simple Terms

```text
TunnelMonitor I2cTask
    -> private SHT3x adapter
        -> SHT3x chip driver
            -> injected, timeout-bounded I2C callbacks
                -> I2cTask-owned shared bus
```

- TunnelMonitor owns the bus, queue, request deadlines, retries, application
  health, and bus recovery.
- The private adapter translates owner requests and transport results. It does
  not retry, recover, or expose library types through public TunnelMonitor
  contracts.
- This library owns SHT3x commands, CRC, command spacing, acquisition modes,
  chip-local state, and chip-local diagnostics. It never owns or reconfigures
  the bus.

## Required Cooperative Flow

1. Provide exact-byte-count, timeout-bounded transport callbacks and the
   required timing hooks. Set `HealthPolicy::OBSERVE_ONLY` so TunnelMonitor
   remains the admission-policy owner.
2. Call `bind()` to validate and store configuration with zero I2C.
3. On initial use and after owner-level bus recovery, submit a nonzero
   `JobRequest` to `requestEnsureIdle()`. Advance it with
   `pollJob(nowMs, 1, result)` until its typed terminal result is returned.
4. For a sample, call `requestMeasurement(JobRequest)` and use the same
   one-callback polling rule. Consume every terminal result immediately and
   verify that its request identity matches the owner's request.
5. Read successful samples with `getMeasurementMilli()`. Nearest rounding
   remains the default; pass `MilliRounding::TRUNCATE_SCALED` when compatibility
   requires truncating the positive scaled ratio before applying the
   temperature offset.
6. Set `singleShotMeasurementMarginMs` explicitly when the owner must preserve
   a fixed normal-VDD conversion wait; the safe default remains 1 ms.
7. Cancel only between polls with `cancelJob()`. Cancellation is bus-silent. Do
   not cancel and forget a job that changed or may have changed hardware state:
   either let it reach its normal terminal result or cancel it and schedule
   ensure-idle reconciliation before assuming the device is idle.

TunnelMonitor may retain 64-bit deadlines and completion timestamps. Pass the
low 32 bits of its bounded deadline into `JobRequest`; the library's deadline
comparisons are wrap-safe over the supported operation interval.

## Error And Presence Rules

- Only a NACK from the discovery probe means expected device absence.
- A command or read NACK after a successful probe is a transfer failure, not
  disappearance from the candidate list.
- Do not advertise `TransportCapability::READ_HEADER_NACK` unless the backend
  can reliably distinguish a read-header NACK from other failures.
- `DriverState::READY` is local library health/admission state. It is not proof
  of current presence or healthy application data.
- General-call reset remains an owner/application policy because it affects the
  shared bus.

## Observed Consumer Status

The local sibling source shows the intended integration completed in software:

- `platformio.ini` exact-pins `SHT3x-main` tag `v1.8.0`.
- `Sht3xModule` privately owns one library instance, injects owner transport and
  timing callbacks, uses passive `bind()`, and advances identified measurement
  jobs through `pollJob()`.
- The product environment owner composes independent `0x44` and `0x45` SHT3x
  candidates with BME280 fallback instead of duplicating SHT3x protocol code.
- Native tests cover binding, measurement timing, NACK classification, CRC and
  timeout boundaries, cancellation, recovery/hotplug, and independent devices.

The remaining consumer work is evidence, not another SHT3x abstraction:

- Run the exact consumer commit's full software matrix after its current local
  changes are finalized.
- Run the production owner, both SHT3x addresses, hotplug/recovery, and
  environment selection end to end on authorized hardware.
- Re-review and deliberately update the exact pin only when a later SHT3x
  release contains changes the consumer needs.

These are consumer validation/release tasks, not unresolved SHT3x core defects.
This library's hardware evidence and limits remain in
[hardware.md](hardware.md).
