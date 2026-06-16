#include "IdfI2cTransport.h"

#include <limits>

#include <esp_err.h>

namespace {

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
    case ESP_ERR_INVALID_RESPONSE:
      return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, message,
                                  static_cast<int32_t>(err));
    default:
      return SHT3x::Status::Error(SHT3x::Err::I2C_BUS, message,
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
  SHT3x::Status st = validate(addr, user);
  if (!st.ok()) {
    return st;
  }
  if (data == nullptr || len == 0U) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                "Invalid IDF I2C write buffer");
  }

  IdfI2cContext* ctx = static_cast<IdfI2cContext*>(user);
  return mapEspError(i2c_master_transmit(ctx->device, data, len, timeoutToIdf(timeoutMs)),
                     "IDF I2C write failed");
}

SHT3x::Status idfI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                              uint8_t* rxData, size_t rxLen,
                              uint32_t timeoutMs, void* user) {
  (void)txData;
  SHT3x::Status st = validate(addr, user);
  if (!st.ok()) {
    return st;
  }
  if (txLen != 0U) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                "SHT3x IDF adapter requires receive-only reads");
  }
  if (rxLen > 0U && rxData == nullptr) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                "Invalid IDF I2C read buffer");
  }
  if (rxLen == 0U) {
    return SHT3x::Status::Ok();
  }

  IdfI2cContext* ctx = static_cast<IdfI2cContext*>(user);
  return mapEspError(i2c_master_receive(ctx->device, rxData, rxLen,
                                        timeoutToIdf(timeoutMs)),
                     "IDF I2C read failed");
}
