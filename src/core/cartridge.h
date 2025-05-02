#pragma once

#include "structures.h"

#include "common/heap_array.h"

#include <ctime>
#include <string>

class Error;
class StateWrapper;

class System;

#define ROM_BANK_SIZE (16384)
#define MAX_NUM_ROM_BANKS (4096)

enum MBC : u8
{
  MBC_NONE,
  MBC_MBC1,
  MBC_MBC2,
  MBC_MBC3,
  MBC_MBC4,
  MBC_MBC5,
  MBC_MMM01,
  NUM_MBC_TYPES
};

struct CartridgeTypeInfo
{
  uint8 id;
  MBC mbc;
  bool ram;
  bool battery;
  bool timer;
  bool rumble;
  const char* description;
};

class Cartridge
{
  friend System;

public:
  Cartridge(std::span<const u8> rom);
  ~Cartridge();

  ALWAYS_INLINE const std::string& GetName() const { return m_name; }
  ALWAYS_INLINE MBC GetMBC() const { return m_mbc; }
  ALWAYS_INLINE SYSTEM_MODE GetSystemMode() const { return m_system_mode; }
  ALWAYS_INLINE u32 GetCRC() const { return m_crc; }
  ALWAYS_INLINE const byte* GetExternalRAM() const { return m_external_ram.data(); }
  ALWAYS_INLINE byte* GetExternalRAM() { return m_external_ram.data(); }
  ALWAYS_INLINE uint32 GetExternalRAMSize() const { return static_cast<u32>(m_external_ram.size()); }
  ALWAYS_INLINE const CartridgeTypeInfo* GetTypeInfo() const { return m_typeinfo; }

  ALWAYS_INLINE const std::span<const u8>& GetROM() const { return m_rom; }
  ALWAYS_INLINE uint32 GetROMBankCount() const { return static_cast<u32>(m_rom_banks.size()); }
  const CART_HEADER* GetHeader() const;
  const byte* GetROMBank(uint32 bank) const;

  bool Load(Error* error);

  // CPU Reads/Writes
  void Reset();
  uint8 CPURead(uint16 address);
  void CPUWrite(uint16 address, uint8 value);

private:
  bool ParseHeader(Error* error);

  // state saving
  bool DoState(StateWrapper& sw);
  void LoadRAM();
  void SaveRAM();
  void LoadRTC();
  void SaveRTC();

  MBC m_mbc = NUM_MBC_TYPES;
  SYSTEM_MODE m_system_mode = SYSTEM_MODE_DMG;
  uint32 m_crc = 0;

  std::span<const u8> m_rom;

  DynamicHeapArray<const byte*> m_rom_banks;

  DynamicHeapArray<byte> m_external_ram;

  const CartridgeTypeInfo* m_typeinfo = nullptr;

  bool m_external_ram_modified;

  // MBC data
  union
  {
    struct
    {
      uint8 active_rom_bank;
      uint8 active_ram_bank;

      bool ram_enable;
      uint8 bank_mode;
      uint8 rom_bank_number;
      uint8 ram_bank_number;
    } mbc1;

    struct
    {
      uint8 rom_bank_number;
      uint8 ram_bank_number;
      bool ram_rtc_enable;

      uint8 rtc_latch;
      uint8 rtc_latch_data[5];
    } mbc3;

    struct
    {
      uint16 active_rom_bank;
      uint16 rom_bank_number;
      uint8 ram_bank_number;
      bool ram_enable;
    } mbc5;
  } m_mbc_data = {};

  // RTC calculator
  struct RTCValue
  {
    uint32 seconds;
    uint32 minutes;
    uint32 hours;
    uint32 days;
  };
  RTCValue GetCurrentRTCTime() const;

  // RTC data
  struct
  {
    time_t base_time;
    uint8 offset_seconds;
    uint8 offset_minutes;
    uint8 offset_hours;
    uint16 offset_days;
    bool active;
  } m_rtc_data = {};

  std::string m_name;

  // MBC_NONE
  bool MBC_NONE_Init();
  void MBC_NONE_Reset();
  uint8 MBC_NONE_Read(uint16 address);
  void MBC_NONE_Write(uint16 address, uint8 value);
  bool MBC_NONE_DoState(StateWrapper& sw);

  // MBC_MBC1
  bool MBC_MBC1_Init();
  void MBC_MBC1_Reset();
  uint8 MBC_MBC1_Read(uint16 address);
  void MBC_MBC1_Write(uint16 address, uint8 value);
  bool MBC_MBC1_DoState(StateWrapper& sw);
  void MBC_MBC1_UpdateActiveBanks();

  // MBC_MBC3
  bool MBC_MBC3_Init();
  void MBC_MBC3_Reset();
  uint8 MBC_MBC3_Read(uint16 address);
  void MBC_MBC3_Write(uint16 address, uint8 value);
  bool MBC_MBC3_DoState(StateWrapper& sw);
  void MBC_MBC3_UpdateActiveBanks();

  // MBC_MBC5
  bool MBC_MBC5_Init();
  void MBC_MBC5_Reset();
  uint8 MBC_MBC5_Read(uint16 address);
  void MBC_MBC5_Write(uint16 address, uint8 value);
  bool MBC_MBC5_DoState(StateWrapper& sw);
  void MBC_MBC5_UpdateActiveBanks();
};
