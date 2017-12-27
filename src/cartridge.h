#pragma once
#include "YBaseLib/Assert.h"
#include "YBaseLib/Common.h"
#include "YBaseLib/String.h"
#include "YBaseLib/Timestamp.h"
#include "structures.h"

class ByteStream;
class BinaryReader;
class BinaryWriter;
class Error;

class System;

#define ROM_BANK_SIZE (16384)
#define MAX_NUM_ROM_BANKS (4096)

enum MBC
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
  Cartridge(System* system);
  ~Cartridge();

  const String& GetName() const { return m_name; }
  const MBC GetMBC() const { return m_mbc; }
  const SYSTEM_MODE GetSystemMode() const { return m_system_mode; }
  const uint32 GetExternalRAMSize() const { return m_external_ram_size; }
  const CartridgeTypeInfo* GetTypeInfo() const { return m_typeinfo; }

  const byte* GetROMBank(uint32 bank) const
  {
    DebugAssert(bank < m_num_rom_banks);
    return m_rom_banks[bank];
  }
  const uint32 GetROMBankCount() const { return m_num_rom_banks; }

  bool Load(ByteStream* pStream, Error* pError);

  // CPU Reads/Writes
  void Reset();
  uint8 CPURead(uint16 address);
  void CPUWrite(uint16 address, uint8 value);

private:
  bool ParseHeader(ByteStream* pStream, Error* pError);

  // state saving
  bool LoadState(ByteStream* pStream, BinaryReader& binaryReader, Error* pError);
  void SaveState(ByteStream* pStream, BinaryWriter& binaryWriter);
  void LoadRAM();
  void SaveRAM();
  void LoadRTC();
  void SaveRTC();

  System* m_system;

  String m_name;
  MBC m_mbc;
  SYSTEM_MODE m_system_mode;
  uint32 m_crc;

  const CartridgeTypeInfo* m_typeinfo;

  byte** m_rom_banks;
  uint32 m_num_rom_banks;

  byte* m_external_ram;
  uint32 m_external_ram_size;
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
  } m_mbc_data;

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
    Timestamp::UnixTimestampValue base_time;
    uint8 offset_seconds;
    uint8 offset_minutes;
    uint8 offset_hours;
    uint16 offset_days;
    bool active;
  } m_rtc_data;

  // MBC_NONE
  bool MBC_NONE_Init();
  void MBC_NONE_Reset();
  uint8 MBC_NONE_Read(uint16 address);
  void MBC_NONE_Write(uint16 address, uint8 value);
  bool MBC_NONE_LoadState(ByteStream* pStream, BinaryReader& binaryReader);
  void MBC_NONE_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter);

  // MBC_MBC1
  bool MBC_MBC1_Init();
  void MBC_MBC1_Reset();
  uint8 MBC_MBC1_Read(uint16 address);
  void MBC_MBC1_Write(uint16 address, uint8 value);
  bool MBC_MBC1_LoadState(ByteStream* pStream, BinaryReader& binaryReader);
  void MBC_MBC1_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter);
  void MBC_MBC1_UpdateActiveBanks();

  // MBC_MBC3
  bool MBC_MBC3_Init();
  void MBC_MBC3_Reset();
  uint8 MBC_MBC3_Read(uint16 address);
  void MBC_MBC3_Write(uint16 address, uint8 value);
  bool MBC_MBC3_LoadState(ByteStream* pStream, BinaryReader& binaryReader);
  void MBC_MBC3_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter);
  void MBC_MBC3_UpdateActiveBanks();

  // MBC_MBC5
  bool MBC_MBC5_Init();
  void MBC_MBC5_Reset();
  uint8 MBC_MBC5_Read(uint16 address);
  void MBC_MBC5_Write(uint16 address, uint8 value);
  bool MBC_MBC5_LoadState(ByteStream* pStream, BinaryReader& binaryReader);
  void MBC_MBC5_SaveState(ByteStream* pStream, BinaryWriter& binaryWriter);
  void MBC_MBC5_UpdateActiveBanks();
};
