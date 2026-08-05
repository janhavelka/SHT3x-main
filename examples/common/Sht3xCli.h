/**
 * @file Sht3xCli.h
 * @brief Arduino bringup CLI command processor for SHT3x diagnostics.
 *
 * NOT part of the library API. This is example/application glue; the ESP-IDF
 * example intentionally uses its own native fixed-buffer CLI.
 */

#pragma once

#include <cstdarg>
#include <cstdint>

#include "SHT3x/SHT3x.h"
#include "TransferStats.h"

namespace sht3x_cli {

using VprintfFn = void (*)(void* user, const char* fmt, va_list args);
using NowMsFn = uint32_t (*)(void* user);
using YieldFn = void (*)(void* user);
using ScanBusFn = void (*)(void* user);

using TransferStats = sht3x_example::TransferStats;

using GetTransferStatsFn = TransferStats (*)(void* user);
using ResetTransferStatsFn = void (*)(void* user);

struct Platform {
  VprintfFn vprintf = nullptr;
  NowMsFn nowMs = nullptr;
  YieldFn yield = nullptr;
  ScanBusFn scanBus = nullptr;
  GetTransferStatsFn getTransferStats = nullptr;
  ResetTransferStatsFn resetTransferStats = nullptr;
  void* user = nullptr;
  const char* framework = nullptr;
  const char* arduinoCoreVersion = nullptr;
  const char* espIdfVersion = nullptr;
  const char* buildTarget = nullptr;
  const char* buildDate = nullptr;
  const char* buildTime = nullptr;
};

SHT3x::Config& config();
bool& configReady();

void setPlatform(const Platform& platform);
void printPrompt();
void printHelp();
void printVersionInfo();
void printDriverHealth();
void processCommand(const char* line);
void tick();
SHT3x::Status beginOwnerSafe();

void logInfo(const char* fmt, ...);
void logWarn(const char* fmt, ...);
void logError(const char* fmt, ...);

}  // namespace sht3x_cli
