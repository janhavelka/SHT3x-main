# Integrating SHT3x Into A Larger Firmware

This guide is for firmware that already owns an I2C bus and a scheduler, and
wants to add SHT3x as one device among several. It is product-neutral; the
reference consumer is a TunnelMonitor-style node with a dedicated I2C task, but
nothing here is specific to it.

If you only want to bring up a sensor on the bench, use the diagnostic CLI
examples instead — see [hardware.md](hardware.md).

## Ownership Boundary

```text
Application I2C task
    -> your private SHT3x adapter
        -> SHT3x driver
            -> injected, timeout-bounded I2C callbacks
                -> the task-owned shared bus
```

- **The application owns** the bus, pins, clock, bus timeouts, the request
  queue, deadlines, retries, application-level health, and bus recovery.
- **Your adapter** translates owner requests and transport results. It should
  not retry, recover, or leak library types through your product's public
  interfaces.
- **This library owns** SHT3x commands, CRC, command spacing, acquisition modes,
  chip-local state, and chip-local diagnostics. It never owns or reconfigures
  the bus.

Keep the driver instance private to one adapter. It is not thread-safe and not
ISR-safe; serialize access in the owning task.

## Cooperative Flow

1. Provide exact-byte-count, timeout-bounded transport callbacks and the three
   required timing hooks (`nowMs`, `nowUs`, `cooperativeYield`). Set
   `HealthPolicy::OBSERVE_ONLY` if your application — not the driver — is the
   admission-policy owner.
2. Call `bind()` to validate and store configuration. This performs zero I2C, so
   it cannot fail because the device is absent.
3. On first use, and after any owner-level bus recovery, submit a nonzero
   `JobRequest` to `requestEnsureIdle()`. Advance it with
   `pollJob(nowMs, 1, result)` until it returns a terminal result. This is the
   only way to establish a verified acquisition baseline.
4. For a sample, call `requestMeasurement(JobRequest)` and poll the same way.
   Consume every terminal result immediately and check that `result.requestId`
   matches the request you issued.
5. Read successful samples with `getMeasurementMilli()` if you want to stay off
   floating point. Nearest rounding is the default; pass
   `MilliRounding::TRUNCATE_SCALED` when an existing contract requires
   truncating the positive scaled ratio before the temperature offset.
6. Set `singleShotMeasurementMarginMs` explicitly if you need a fixed
   conversion wait. The default is 1 ms on top of the datasheet maximum.
7. Cancel only between polls, with `cancelJob()`. Cancellation is bus-silent.
   Do not cancel and forget a job that may have changed hardware state: either
   let it reach its terminal result, or cancel it and then schedule
   `requestEnsureIdle()` before assuming the device is idle.

Each `pollJob()` performs at most one transport callback. Wait phases —
conversion, reset settle, command spacing — perform none.

If your scheduler keeps 64-bit deadlines, pass the low 32 bits into
`JobRequest::deadlineMs`. The driver's comparisons are wrap-safe as long as the
deadline is within `INT32_MAX` milliseconds of the request.

## Transport Contract

Return the most specific error your backend can *prove*:

| Code | Meaning |
| --- | --- |
| `Err::I2C_NACK_ADDR` | address NACK |
| `Err::I2C_NACK_DATA` | data NACK |
| `Err::I2C_NACK_READ` | read-header NACK |
| `Err::I2C_TIMEOUT` | transfer timeout |
| `Err::I2C_BUS` | bus or arbitration error |
| `Err::I2C_ERROR` | anything the backend cannot distinguish |

Rules that matter in practice:

- The driver issues a command write, waits, then calls `i2cWriteRead()` with
  `txLen == 0`. Never implement a combined write+read with a repeated start.
- Do not advertise `TransportCapability::READ_HEADER_NACK` unless the backend
  really can distinguish a read-header NACK from every other failure. Arduino
  `Wire` cannot: a zero-length `requestFrom()` is ambiguous, so it must map to
  `Err::I2C_ERROR`.
- Callbacks must return within `timeoutMs`, must not block unbounded, and must
  not call back into the same driver instance.
- Callbacks must not own or reconfigure bus pins, the reset pin, global bus
  timeouts, or any other bus-manager policy.

## Presence And Health

- Only a NACK from a discovery probe means the device is genuinely absent. A
  command or read NACK *after* a successful probe is a transfer failure, not a
  disappearance.
- `DriverState::READY` is local health and admission state. It is not proof of
  presence, and not proof that the application's data is good.
- `hardwareStateValid()` is the separate question of whether the driver has
  verified the sensor's acquisition state. Passive `bind()` leaves it false.
- General-call reset affects every device on the bus, so it stays an
  application/bus-manager decision. It is disabled by default.

## Pinning

Pin the reviewed immutable commit in production firmware:

```ini
lib_deps =
  https://github.com/janhavelka/SHT3x-main.git#<reviewed-full-commit-sha>
```

A release tag is only equivalent to a commit pin if you have verified which
commit that tag actually points at.
