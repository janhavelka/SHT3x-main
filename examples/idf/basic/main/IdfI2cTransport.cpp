#include "IdfI2cTransport.h"

#include <limits>

#include <esp_err.h>

namespace {

uint32_t saturatingAdd(uint32_t value, size_t increment) {
  constexpr uint32_t MAX_VALUE = std::numeric_limits<uint32_t>::max();
  if (increment > static_cast<size_t>(MAX_VALUE - value)) {
    return MAX_VALUE;
  }
  return value + static_cast<uint32_t>(increment);
}

SHT3x::Status recordTransfer(IdfI2cContext* ctx, SHT3x::Status status,
                             bool readCallback, size_t txBytes,
                             size_t rxBytes) {
  if (ctx == nullptr) {
    return status;
  }
  sht3x_example::TransferStats& stats = ctx->transferStats;
  if (readCallback) {
    stats.readCallbacks = saturatingAdd(stats.readCallbacks, 1U);
  } else {
    stats.writeCallbacks = saturatingAdd(stats.writeCallbacks, 1U);
  }
  if (status.ok()) {
    stats.successes = saturatingAdd(stats.successes, 1U);
  } else {
    stats.failures = saturatingAdd(stats.failures, 1U);
  }
  stats.txBytes = saturatingAdd(stats.txBytes, txBytes);
  stats.rxBytes = saturatingAdd(stats.rxBytes, rxBytes);
  return status;
}

int timeoutToIdf(uint32_t timeoutMs) {
  constexpr uint32_t MAX_TIMEOUT_MS =
      static_cast<uint32_t>(std::numeric_limits<int>::max());
  if (timeoutMs > MAX_TIMEOUT_MS) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(timeoutMs);
}

SHT3x::Status mapEspError(esp_err_t err, const char* message) {
  switch (err) {
    case ESP_OK:
      return SHT3x::Status::Ok();
    case ESP_ERR_TIMEOUT:
      return SHT3x::Status::Error(SHT3x::Err::I2C_TIMEOUT, message,
                                  static_cast<int32_t>(err));
    case ESP_ERR_INVALID_ARG:
      return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, message,
                                  static_cast<int32_t>(err));
    default:
      // i2c_master_transmit()/i2c_master_receive() do not document a distinct
      // return code per NACK phase, and the code they use for a NACK differs
      // across ESP-IDF 5.x releases. Anything this adapter has not proven is
      // therefore Err::I2C_ERROR, the documented "cannot distinguish the cause"
      // value. Err::I2C_BUS would claim a bus/arbitration fault we cannot prove,
      // and it would also exclude a normal periodic Fetch Data NACK from the
      // driver's bounded no-data inference, which only fires on I2C_ERROR.
      return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, message,
                                  static_cast<int32_t>(err));
  }
}

SHT3x::Status validate(uint8_t addr, const void* user) {
  if (user == nullptr) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_CONFIG,
                                "IDF I2C context is null");
  }
  const IdfI2cContext* ctx = static_cast<const IdfI2cContext*>(user);
  if (ctx->device == nullptr) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_CONFIG,
                                "IDF I2C device handle is null");
  }
  if (addr != ctx->address) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                "Unexpected I2C address");
  }
  return SHT3x::Status::Ok();
}

}  // namespace

SHT3x::Status idfI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                          uint32_t timeoutMs, void* user) {
  IdfI2cContext* ctx = static_cast<IdfI2cContext*>(user);
  SHT3x::Status st = validate(addr, user);
  if (!st.ok()) {
    return recordTransfer(ctx, st, false, 0U, 0U);
  }
  if (data == nullptr || len == 0U) {
    return recordTransfer(
        ctx,
        SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                             "Invalid IDF I2C write buffer"),
        false, 0U, 0U);
  }

  const SHT3x::Status result = mapEspError(
      i2c_master_transmit(ctx->device, data, len, timeoutToIdf(timeoutMs)),
      "IDF I2C write failed");
  return recordTransfer(ctx, result, false, result.ok() ? len : 0U, 0U);
}

SHT3x::Status idfI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                              uint8_t* rxData, size_t rxLen,
                              uint32_t timeoutMs, void* user) {
  (void)txData;
  IdfI2cContext* ctx = static_cast<IdfI2cContext*>(user);
  SHT3x::Status st = validate(addr, user);
  if (!st.ok()) {
    return recordTransfer(ctx, st, true, txLen, 0U);
  }
  if (txLen != 0U) {
    return recordTransfer(
        ctx,
        SHT3x::Status::Error(
            SHT3x::Err::INVALID_PARAM,
            "SHT3x IDF adapter requires receive-only reads"),
        true, txLen, 0U);
  }
  if (rxLen > 0U && rxData == nullptr) {
    return recordTransfer(
        ctx,
        SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                             "Invalid IDF I2C read buffer"),
        true, txLen, 0U);
  }
  if (rxLen == 0U) {
    return recordTransfer(ctx, SHT3x::Status::Ok(), true, txLen, 0U);
  }

  const SHT3x::Status result = mapEspError(
      i2c_master_receive(ctx->device, rxData, rxLen, timeoutToIdf(timeoutMs)),
      "IDF I2C read failed");
  return recordTransfer(ctx, result, true, txLen, result.ok() ? rxLen : 0U);
}
