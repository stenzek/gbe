// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#include "log.h"
#include "assert.h"
#include "timer.h"

#include "retro2/retro2_log.h"

#include "fmt/format.h"

#include <bitset>
#include <iterator>

namespace Log {

using ChannelBitSet = std::bitset<static_cast<size_t>(Channel::MaxCount)>;

static bool FilterTest(Channel channel, Level level);

#if 0
static constexpr const std::array<const char*, static_cast<size_t>(Channel::MaxCount)> s_log_channel_names = {{
#define LOG_CHANNEL_NAME(X) #X,
  ENUMERATE_LOG_CHANNELS(LOG_CHANNEL_NAME)
#undef LOG_CHANNEL_NAME
}};
#endif

namespace {

struct State
{
  IRetro2Log r2log;
  Level log_level = Level::Trace;

  ChannelBitSet log_channels_enabled = ChannelBitSet().set();
};

} // namespace

ALIGN_TO_CACHE_LINE static State s_state;

} // namespace Log

Log::Level Log::GetLogLevel()
{
  return s_state.log_level;
}

bool Log::IsLogVisible(Level level, Channel channel)
{
  return FilterTest(channel, level);
}

void Log::Initialize(const IRetro2Log* r2log)
{
  s_state.r2log = *r2log;
  s_state.log_level = static_cast<Log::Level>(s_state.r2log.GetLevel());
}

void Log::UpdateSettings()
{
  // TODO: channel visibility
  s_state.log_level = static_cast<Log::Level>(s_state.r2log.GetLevel());
}

ALWAYS_INLINE_RELEASE bool Log::FilterTest(Channel channel, Level level)
{
  return (level <= s_state.log_level && s_state.log_channels_enabled[static_cast<size_t>(channel)]);
}

void Log::Write(MessageCategory cat, std::string_view message)
{
  if (!FilterTest(UnpackChannel(cat), UnpackLevel(cat)))
    return;

  s_state.r2log.ColorWriteLength(static_cast<RETRO2_LOG_LEVEL>(UnpackLevel(cat)),
                                 static_cast<RETRO2_LOG_COLOR>(UnpackColor(cat)), message.data(), message.length());
}

void Log::Write(MessageCategory cat, const char* functionName, std::string_view message)
{
  if (!FilterTest(UnpackChannel(cat), UnpackLevel(cat)))
    return;

  fmt::memory_buffer buffer;
  fmt::format_to(std::back_inserter(buffer), "[{}] {}", std::string_view(functionName), message);
  s_state.r2log.ColorWriteLength(static_cast<RETRO2_LOG_LEVEL>(UnpackLevel(cat)),
                                 static_cast<RETRO2_LOG_COLOR>(UnpackColor(cat)), buffer.data(), buffer.size());
}

void Log::WriteFmtArgs(MessageCategory cat, fmt::string_view fmt, fmt::format_args args)
{
  if (!FilterTest(UnpackChannel(cat), UnpackLevel(cat)))
    return;

  fmt::memory_buffer buffer;
  fmt::vformat_to(std::back_inserter(buffer), fmt, args);

  s_state.r2log.ColorWriteLength(static_cast<RETRO2_LOG_LEVEL>(UnpackLevel(cat)),
                                 static_cast<RETRO2_LOG_COLOR>(UnpackColor(cat)), buffer.data(), buffer.size());
}

void Log::WriteFmtArgs(MessageCategory cat, const char* functionName, fmt::string_view fmt, fmt::format_args args)
{
  if (!FilterTest(UnpackChannel(cat), UnpackLevel(cat)))
    return;

  fmt::memory_buffer buffer;
  fmt::format_to(std::back_inserter(buffer), "[{}] ", std::string_view(functionName));
  fmt::vformat_to(std::back_inserter(buffer), fmt, args);

  s_state.r2log.ColorWriteLength(static_cast<RETRO2_LOG_LEVEL>(UnpackLevel(cat)),
                                 static_cast<RETRO2_LOG_COLOR>(UnpackColor(cat)), buffer.data(), buffer.size());
}
