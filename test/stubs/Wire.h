/// @file Wire.h
/// @brief Minimal Wire stub for native testing
#pragma once

#include <cstdint>
#include <cstddef>

class TwoWire {
public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0U) {
    (void)sda;
    (void)scl;
    (void)frequency;
    return _beginResult;
  }
  void setClock(uint32_t freq) { _clockHz = freq; _clockSetCalls++; }
  void setTimeOut(uint32_t timeoutMs) { _timeoutMs = timeoutMs; _timeoutSetCalls++; }
  uint32_t getTimeOut() const { return _timeoutMs; }
  uint32_t getClock() const { return _clockHz; }
  uint32_t _clockSetCount() const { return _clockSetCalls; }
  void _clearClockSetCount() { _clockSetCalls = 0; }
  uint32_t _timeoutSetCount() const { return _timeoutSetCalls; }
  void _clearTimeoutSetCount() { _timeoutSetCalls = 0; }
  
  void beginTransmission(uint8_t addr) { _busCalls++; _addr = addr; _txLen = 0; }
  size_t write(uint8_t data) { _txBuf[_txLen++] = data; return 1; }
  size_t write(const uint8_t* data, size_t len) { 
    for (size_t i = 0; i < len && _txLen < sizeof(_txBuf); i++) {
      _txBuf[_txLen++] = data[i];
    }
    return len;
  }
  uint8_t endTransmission(bool stop = true) {
    _busCalls++;
    if (_transferHook != nullptr) _transferHook(_timeoutMs);
    _lastStop = stop;
    if (_useAckAddress) {
      return _addr == _ackAddress ? 0U : 2U;
    }
    return 0U;
  }
  
  size_t requestFrom(uint8_t addr, size_t len) { 
    _busCalls++;
    if (_transferHook != nullptr) _transferHook(_timeoutMs);
    (void)addr;
    const size_t result = _useRequestFromOverride ? _requestFromResult : len;
    _rxLen = result;
    _rxIdx = 0;
    return result;
  }
  
  int available() { return _rxLen - _rxIdx; }
  int read() { 
    if (_rxIdx < _rxLen) {
      _readCalls++;
      return _rxBuf[_rxIdx++];
    }
    return -1;
  }

  // Test helper: set data to return on next read
  void _setReadData(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && i < sizeof(_rxBuf); i++) {
      _rxBuf[i] = data[i];
    }
  }

  void _setRequestFromResult(size_t result) {
    _useRequestFromOverride = true;
    _requestFromResult = result;
  }

  void _clearRequestFromOverride() {
    _useRequestFromOverride = false;
    _requestFromResult = 0;
  }

  void _setBeginResult(bool result) { _beginResult = result; }

  void _setAckAddress(uint8_t address) {
    _useAckAddress = true;
    _ackAddress = address;
  }

  bool _lastStopWasTrue() const { return _lastStop; }
  uint32_t _readCallCount() const { return _readCalls; }
  void _clearReadCallCount() { _readCalls = 0; }
  uint32_t _busCallCount() const { return _busCalls; }
  void _setTransferHook(void (*hook)(uint32_t)) { _transferHook = hook; }

private:
  uint8_t _addr = 0;
  uint8_t _txBuf[64] = {};
  size_t _txLen = 0;
  uint8_t _rxBuf[64] = {};
  size_t _rxLen = 0;
  size_t _rxIdx = 0;
  uint32_t _timeoutMs = 0;
  uint32_t _clockHz = 0;
  uint32_t _clockSetCalls = 0;
  uint32_t _timeoutSetCalls = 0;
  bool _lastStop = true;
  uint32_t _readCalls = 0;
  uint32_t _busCalls = 0;
  void (*_transferHook)(uint32_t) = nullptr;
  bool _useRequestFromOverride = false;
  size_t _requestFromResult = 0;
  bool _beginResult = true;
  bool _useAckAddress = false;
  uint8_t _ackAddress = 0;
};

extern TwoWire Wire;
