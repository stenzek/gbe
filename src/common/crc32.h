#pragma once

#include "types.h"

#include <span>

class CRC32
{
public:
  CRC32(u32 start = 0);

  ALWAYS_INLINE u32 GetCRC() const { return m_crc; }

  static u32 GetHash(const void* buf, size_t len);
  static u32 GetHash(std::span<const u8> buf);

  void HashBytes(const void* buf, size_t len);
  void HashBytes(std::span<const u8> buf);
  void Reset();

private:
  u32 m_crc;
};
