// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#pragma once

#include "log_channels.h"
#include "types.h"

#include "fmt/base.h"

#include <array>
#include <cinttypes>
#include <cstdarg>
#include <mutex>
#include <string_view>

struct IRetro2Log;

namespace Log {
enum class Level : u32
{
  None, // Silences all log traffic
  Error,
  Warning,
  Info,
  Verbose,
  Dev,
  Debug,
  Trace,

  MaxCount
};

enum class Color : u32
{
  Default,
  Black,
  Green,
  Red,
  Blue,
  Magenta,
  Orange,
  Cyan,
  Yellow,
  White,
  StrongBlack,
  StrongRed,
  StrongGreen,
  StrongBlue,
  StrongMagenta,
  StrongOrange,
  StrongCyan,
  StrongYellow,
  StrongWhite,

  MaxCount
};

enum class Channel : u32
{
#define LOG_CHANNEL_ENUM(X) X,
  ENUMERATE_LOG_CHANNELS(LOG_CHANNEL_ENUM)
#undef LOG_CHANNEL_ENUM

    MaxCount
};

// Packs a level and channel into one 16-bit number.
using MessageCategory = u32;
[[maybe_unused]] ALWAYS_INLINE static constexpr u32 PackCategory(Channel channel, Level level, Color colour)
{
  return ((static_cast<MessageCategory>(colour) << 10) | (static_cast<MessageCategory>(channel) << 3) |
          static_cast<MessageCategory>(level));
}
[[maybe_unused]] ALWAYS_INLINE static constexpr Color UnpackColor(MessageCategory cat)
{
  return static_cast<Color>((cat >> 10) & 0x1f);
}
[[maybe_unused]] ALWAYS_INLINE static constexpr Channel UnpackChannel(MessageCategory cat)
{
  return static_cast<Channel>((cat >> 3) & 0x7f);
}
[[maybe_unused]] ALWAYS_INLINE static constexpr Level UnpackLevel(MessageCategory cat)
{
  return static_cast<Level>(cat & 0x7);
}

// Returns the current global filtering level.
Level GetLogLevel();

// Returns true if log messages for the specified log level/filter would not be filtered (and visible).
bool IsLogVisible(Level level, Channel channel);

// Initializes the log forwarder.
void Initialize(const IRetro2Log* r2log);
void UpdateSettings();

// writes a message to the log
void Write(MessageCategory cat, std::string_view message);
void Write(MessageCategory cat, const char* functionName, std::string_view message);
void WriteFmtArgs(MessageCategory cat, fmt::string_view fmt, fmt::format_args args);
void WriteFmtArgs(MessageCategory cat, const char* functionName, fmt::string_view fmt, fmt::format_args args);

ALWAYS_INLINE void FastWrite(Channel channel, Level level, std::string_view message)
{
  if (level <= GetLogLevel()) [[unlikely]]
    Write(PackCategory(channel, level, Color::Default), message);
}
ALWAYS_INLINE void FastWrite(Channel channel, const char* functionName, Level level, std::string_view message)
{
  if (level <= GetLogLevel()) [[unlikely]]
    Write(PackCategory(channel, level, Color::Default), functionName, message);
}
template<typename... T>
ALWAYS_INLINE void FastWrite(Channel channel, Level level, fmt::format_string<T...> fmt, T&&... args)
{
  if (level <= GetLogLevel()) [[unlikely]]
    WriteFmtArgs(PackCategory(channel, level, Color::Default), fmt, fmt::make_format_args(args...));
}
template<typename... T>
ALWAYS_INLINE void FastWrite(Channel channel, const char* functionName, Level level, fmt::format_string<T...> fmt,
                             T&&... args)
{
  if (level <= GetLogLevel()) [[unlikely]]
    WriteFmtArgs(PackCategory(channel, level, Color::Default), functionName, fmt, fmt::make_format_args(args...));
}
ALWAYS_INLINE void FastWrite(Channel channel, Level level, Color colour, std::string_view message)
{
  if (level <= GetLogLevel()) [[unlikely]]
    Write(PackCategory(channel, level, colour), message);
}
ALWAYS_INLINE void FastWrite(Channel channel, const char* functionName, Level level, Color colour,
                             std::string_view message)
{
  if (level <= GetLogLevel()) [[unlikely]]
    Write(PackCategory(channel, level, colour), functionName, message);
}
template<typename... T>
ALWAYS_INLINE void FastWrite(Channel channel, Level level, Color colour, fmt::format_string<T...> fmt, T&&... args)
{
  if (level <= GetLogLevel()) [[unlikely]]
    WriteFmtArgs(PackCategory(channel, level, colour), fmt, fmt::make_format_args(args...));
}
template<typename... T>
ALWAYS_INLINE void FastWrite(Channel channel, const char* functionName, Level level, Color colour,
                             fmt::format_string<T...> fmt, T&&... args)
{
  if (level <= GetLogLevel()) [[unlikely]]
    WriteFmtArgs(PackCategory(channel, level, colour), functionName, fmt, fmt::make_format_args(args...));
}
} // namespace Log

// log wrappers
#define LOG_CHANNEL(name) [[maybe_unused]] static constexpr Log::Channel ___LogChannel___ = Log::Channel::name;

#define ERROR_LOG(...) Log::FastWrite(___LogChannel___, __func__, Log::Level::Error, __VA_ARGS__)
#define WARNING_LOG(...) Log::FastWrite(___LogChannel___, __func__, Log::Level::Warning, __VA_ARGS__)
#define INFO_LOG(...) Log::FastWrite(___LogChannel___, Log::Level::Info, __VA_ARGS__)
#define VERBOSE_LOG(...) Log::FastWrite(___LogChannel___, Log::Level::Verbose, __VA_ARGS__)
#define DEV_LOG(...) Log::FastWrite(___LogChannel___, Log::Level::Dev, __VA_ARGS__)

#if defined(_DEBUG) || defined(_DEVEL)
#define DEBUG_LOG(...) Log::FastWrite(___LogChannel___, Log::Level::Debug, __VA_ARGS__)
#define TRACE_LOG(...) Log::FastWrite(___LogChannel___, Log::Level::Trace, __VA_ARGS__)
#else
#define DEBUG_LOG(...)                                                                                                 \
  do                                                                                                                   \
  {                                                                                                                    \
  } while (0)
#define TRACE_LOG(...)                                                                                                 \
  do                                                                                                                   \
  {                                                                                                                    \
  } while (0)
#endif

// clang-format off
#define ERROR_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, __func__, Log::Level::Error, Log::Color::colour, __VA_ARGS__)
#define WARNING_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, __func__, Log::Level::Warning, Log::Color::colour, __VA_ARGS__)
#define INFO_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, Log::Level::Info, Log::Color::colour, __VA_ARGS__)
#define VERBOSE_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, Log::Level::Verbose, Log::Color::colour, __VA_ARGS__)
#define DEV_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, Log::Level::Dev, Log::Color::colour, __VA_ARGS__)

#if defined(_DEBUG) || defined(_DEVEL)
#define DEBUG_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, Log::Level::Debug, Log::Color::colour, __VA_ARGS__)
#define TRACE_COLOR_LOG(colour, ...) Log::FastWrite(___LogChannel___, Log::Level::Trace, Log::Color::colour,__VA_ARGS__)
#else
#define DEBUG_COLOR_LOG(colour, ...)                                                                                   \
  do                                                                                                                   \
  {                                                                                                                    \
  } while (0)
#define TRACE_COLOR_LOG(colour, ...)                                                                                   \
  do                                                                                                                   \
  {                                                                                                                    \
  } while (0)
#endif

// clang-format on
