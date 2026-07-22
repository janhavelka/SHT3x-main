# TunnelMonitor-node Integration

Last updated: 2026-07-22

This guide describes the current integration boundary between SHT3x `1.7.0`
and TunnelMonitor-node. The library is suitable for the owner's cooperative
I2C model, but TunnelMonitor-node does not yet use this library in production.
No TunnelMonitor-node source, branch, or repository state is changed by this
guide.

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
5. Read successful samples with `getMeasurementMilli()`. The library rounds to
   the nearest milli-unit; a legacy truncating codec can differ by one unit.
6. Cancel only between polls with `cancelJob()`. Cancellation is bus-silent. Do
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

## Work Still Outside This Library

- Approve and exact-pin the SHT3x dependency in TunnelMonitor-node.
- Implement an owner-private adapter and phase-aware post-probe NACK mapping.
- Add native parity tests, then delete the duplicated direct SHT3x protocol
  only after the adapter is authoritative.
- Run the production TunnelMonitor task and adapter end to end on hardware.

Those are consumer integration tasks, not unresolved SHT3x core defects. The
current hardware evidence and its limits are maintained in
[hardware.md](hardware.md).
