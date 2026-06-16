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

namespace sht3x_cli {

using VprintfFn = void (*)(void* user, const char* fmt, va_list args);
using NowMsFn = uint32_t (*)(void* user);
using YieldFn = void (*)(void* user);
using ScanBusFn = void (*)(void* user);

struct Platform {
  VprintfFn vprintf = nullptr;
  NowMsFn nowMs = nullptr;
  YieldFn yield = nullptr;
  ScanBusFn scanBus = nullptr;
  void* user = nullptr;
  const char* buildDate = nullptr;
  const char* buildTime = nullptr;
};

SHT3x::SHT3x& device();
SHT3x::Config& config();
bool& configReady();

void setPlatform(const Platform& platform);
void printPrompt();
void printHelp();
void printVersionInfo();
void printDriverHealth();
void processCommand(const char* line);
void tick();
void cancelPending();

void logInfo(const char* fmt, ...);
void logWarn(const char* fmt, ...);
void logError(const char* fmt, ...);

}  // namespace sht3x_cli
