#pragma once
#include "YBaseLib/Common.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/String.h"

class ByteStream;
class Error;

#define ROM_BANK_SIZE (16384)
#define MAX_NUM_ROM_BANKS (256)

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
    const char *description;
};

class Cartridge
{
public:
    static const uint32 ROM0_SIZE = 0x4000;
    static const uint32 ROM1_SIZE = 0x4000;

public:
    Cartridge();
    ~Cartridge();

    const String &GetName() const { return m_name; }
    const MBC GetMBC() const { return m_mbc; }
    const uint32 GetExternalRAMSize() const { return m_external_ram_size; }
    const CartridgeTypeInfo *GetTypeInfo() const { return m_typeinfo; }

    const byte *GetROMBank(uint32 bank) const { DebugAssert(bank < m_num_rom_banks); return m_rom_banks[bank]; }
    const uint32 GetROMBankCount() const { return m_num_rom_banks; }

    bool Load(ByteStream *pStream, Error *pError);

private:
    bool ParseHeader(ByteStream *pStream, Error *pError);

    String m_name;
    MBC m_mbc;
    uint32 m_external_ram_size;

    const CartridgeTypeInfo *m_typeinfo;

    byte *m_rom_banks[MAX_NUM_ROM_BANKS];
    uint32 m_num_rom_banks;

};
