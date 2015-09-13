#include "cartridge.h"
#include "structures.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/Error.h"
#include "YBaseLib/String.h"
#include "YBaseLib/StringConverter.h"
#include "YBaseLib/Log.h"
Log_SetChannel(Cartridge);

// http://bgb.bircd.org/pandocs.htm#thecartridgeheader
static const CartridgeTypeInfo CART_TYPEINFOS[] =
{
    // id       mbc             ram     battery     timer   rumble
    { 0x00,     MBC_NONE,       false,  false,      false,  false,      "ROM ONLY"                  },    // 00h  ROM ONLY
    { 0x01,     MBC_MBC1,       false,  false,      false,  false,      "MBC1"                      },    // 01h  MBC1
    { 0x02,     MBC_MBC1,       true,   false,      false,  false,      "MBC1+RAM"                  },    // 02h  MBC1+RAM
    { 0x03,     MBC_MBC1,       true,   true,       false,  false,      "MBC1+RAM+BATTERY"          },    // 03h  MBC1+RAM+BATTERY
    { 0x05,     MBC_MBC2,       false,  false,      false,  false,      "MBC2"                      },    // 05h  MBC2
    { 0x06,     MBC_MBC2,       false,  true,       false,  false,      "MBC2+BATTERY"              },    // 06h  MBC2+BATTERY
    { 0x08,     MBC_NONE,       true,   false,      false,  false,      "ROM+RAM"                   },    // 08h  ROM+RAM
    { 0x09,     MBC_NONE,       true,   true,       false,  false,      "ROM+RAM+BATTERY"           },    // 09h  ROM+RAM+BATTERY
    { 0x0B,     MBC_MMM01,      false,  false,      false,  false,      "MMM01"                     },    // 0Bh  MMM01
    { 0x0C,     MBC_MMM01,      true,   false,      false,  false,      "MMM01+RAM"                 },    // 0Ch  MMM01+RAM
    { 0x0D,     MBC_MMM01,      true,   true,       false,  false,      "MMM01+RAM+BATTERY"         },    // 0Dh  MMM01+RAM+BATTERY
    { 0x0F,     MBC_MBC3,       false,  true,       true,   false,      "MBC3+TIMER+BATTERY"        },    // 0Fh  MBC3+TIMER+BATTERY
    { 0x10,     MBC_MBC3,       true,   true,       true,   false,      "MBC3+TIMER+RAM+BATTERY"    },    // 10h  MBC3+TIMER+RAM+BATTERY
    { 0x11,     MBC_MBC3,       false,  false,      false,  false,      "MBC3"                      },    // 11h  MBC3
    { 0x12,     MBC_MBC3,       true,   false,      false,  false,      "MBC3+RAM"                  },    // 12h  MBC3+RAM
    { 0x13,     MBC_MBC3,       true,   true,       false,  false,      "MBC3+RAM+BATTERY"          },    // 13h  MBC3+RAM+BATTERY
    { 0x15,     MBC_MBC4,       false,  false,      false,  false,      "MBC4"                      },    // 15h  MBC4
    { 0x16,     MBC_MBC4,       true,   false,      false,  false,      "MBC4+RAM"                  },    // 16h  MBC4+RAM
    { 0x17,     MBC_MBC4,       true,   true,       false,  false,      "MBC4+RAM+BATTERY"          },    // 17h  MBC4+RAM+BATTERY
    { 0x19,     MBC_MBC5,       false,  false,      false,  false,      "MBC5"                      },    // 19h  MBC5
    { 0x1A,     MBC_MBC5,       true,   false,      false,  false,      "MBC5+RAM"                  },    // 1Ah  MBC5+RAM
    { 0x1B,     MBC_MBC5,       true,   true,       false,  false,      "MBC5+RAM+BATTERY"          },    // 1Bh  MBC5+RAM+BATTERY
    { 0x1C,     MBC_MBC5,       false,  false,      false,  true,       "MBC5+RUMBLE"               },    // 1Ch  MBC5+RUMBLE
    { 0x1D,     MBC_MBC5,       true,   false,      false,  true,       "MBC5+RUMBLE+RAM"           },    // 1Dh  MBC5+RUMBLE+RAM
    { 0x1E,     MBC_MBC5,       true,   true,       false,  true,       "MBC5+RUMBLE+RAM+BATTERY"   },    // 1Eh  MBC5+RUMBLE+RAM+BATTERY
};
static const char *MBC_NAME_STRINGS[NUM_MBC_TYPES] =
{
    "MBC_NONE",
    "MBC_MBC1",
    "MBC_MBC2",
    "MBC_MBC3",
    "MBC_MBC4",
    "MBC_MMM01",
};
static const uint32 CART_EXTERNAL_RAM_SIZES[4] = { 0, 2048 /* 2KB */, 8192 /* 8KB */, 32768 /* 32KB */ };
static const uint32 CART_ROM_BANK_COUNT[][2] =
{
    { 0x00, 2 }, // no rom banks
    { 0x01, 4 },
    { 0x02, 8 },
    { 0x03, 16 },
    { 0x04, 32 },
    { 0x05, 64 },
    { 0x06, 128 },
    { 0x07, 256 },
    { 0x52, 72 },
    { 0x53, 80 },
    { 0x54, 96 },
};

Cartridge::Cartridge()
    : m_external_ram_size(0)
    , m_typeinfo(nullptr)
    , m_num_rom_banks(0)
{
    Y_memzero(m_rom_banks, sizeof(m_rom_banks));
}

Cartridge::~Cartridge()
{
    for (uint32 i = 0; i < m_num_rom_banks; i++)
        Y_free(m_rom_banks[i]);
}

bool Cartridge::ParseHeader(ByteStream *pStream, Error *pError)
{
    SmallString str;
    CART_HEADER header;
    if (!pStream->SeekAbsolute(CART_HEADER_OFFSET) || !pStream->Read2(&header, sizeof(header)))
    {
        pError->SetErrorUser(1, "Failed to read cartridge header");
        return false;
    }

    Log_InfoPrint("Cartridge info: ");
    
    str.Clear(); str.AppendString(header.title, sizeof(header.title));
    Log_InfoPrintf("  Title: %s", str.GetCharArray());
    
    str.Clear(); str.AppendString(header.cgb_title, sizeof(header.cgb_title));
    Log_InfoPrintf("  CGB Title: %s", str.GetCharArray());

    str.Clear(); str.AppendString(header.cgb_manufacturer, sizeof(header.cgb_manufacturer));
    Log_InfoPrintf("  CGB Manufacturer: %s", str.GetCharArray());

    Log_InfoPrintf("  CGB Flag: 0x%02X", header.cgb_flag);
    Log_InfoPrintf("  CGB Licensee code: %c%c", header.cgb_licensee_code[0], header.cgb_licensee_code[1]);
    Log_InfoPrintf("  SGB Flag: 0x%02X", header.sgb_flag);
    Log_InfoPrintf("  Type: 0x%02X", header.type);
    Log_InfoPrintf("  ROM Size Code: 0x%02X", header.rom_size);
    Log_InfoPrintf("  RAM Size Code: 0x%02X", header.ram_size);
    Log_InfoPrintf("  Region Code: 0x%02X", header.region_code);
    Log_InfoPrintf("  Licensee Code: 0x%02X", header.licensee_code);
    Log_InfoPrintf("  ROM Version: 0x%02X", header.rom_version);
    Log_InfoPrintf("  Header Checksum: 0x%02X", header.header_checksum);
    Log_InfoPrintf("  Cartridge Checksum: 0x%04X", header.cartridge_checksum);

    // set name
    m_name.Clear();
    if ((header.cgb_flag & 0x80) || (header.cgb_flag & 0xC0))
        m_name.AppendString(header.cgb_title, sizeof(header.cgb_title));
    else
        m_name.AppendString(header.title, sizeof(header.title));
    m_name.UpdateSize();

    // get info
    for (uint32 i = 0; i < countof(CART_TYPEINFOS); i++)
    {
        if (CART_TYPEINFOS[i].id == header.type)
        {
            m_typeinfo = &CART_TYPEINFOS[i];
            break;
        }
    }
    if (m_typeinfo == nullptr)
    {
        pError->SetErrorUserFormatted(1, "Unknown cartridge type: 0x%02X", header.type);
        return false;
    }

    // dump cart type info
    Log_InfoPrintf("  Cartridge type description: %s", m_typeinfo->description);
    Log_InfoPrintf("    ID: 0x%02X", m_typeinfo->id);
    Log_InfoPrintf("    Memory bank controller: %s", MBC_NAME_STRINGS[m_typeinfo->mbc]);
    Log_InfoPrintf("    External RAM: %s", m_typeinfo->ram ? "yes" : "no");
    Log_InfoPrintf("    Battery: %s", m_typeinfo->battery ? "yes" : "no");
    Log_InfoPrintf("    Timer: %s", m_typeinfo->timer ? "yes" : "no");
    Log_InfoPrintf("    Rumble: %s", m_typeinfo->rumble ? "yes" : "no");
    m_mbc = m_typeinfo->mbc;

    // parse rom banks
    m_num_rom_banks = 0;
    for (uint32 i = 0; i < countof(CART_ROM_BANK_COUNT); i++)
    {
        if (CART_ROM_BANK_COUNT[i][0] == header.rom_size)
        {
            m_num_rom_banks = CART_ROM_BANK_COUNT[i][1];
            break;
        }
    }
    if (m_num_rom_banks == 0)
    {
        pError->SetErrorUserFormatted(1, "Unknown rom size code: 0x%02X", header.rom_size);
        return false;
    }
    Log_InfoPrintf("  ROM Banks: %u (%s)", m_num_rom_banks, StringConverter::SizeToHumanReadableString(ROM_BANK_SIZE * m_num_rom_banks).GetCharArray());

    // parse ram
    if (header.ram_size >= countof(CART_EXTERNAL_RAM_SIZES) || (header.ram_size > 0 && !m_typeinfo->ram))
    {
        pError->SetErrorUserFormatted(1, "Unknown ram size code: %02X", header.ram_size);
        return false;
    }
    m_external_ram_size = CART_EXTERNAL_RAM_SIZES[header.ram_size];
    Log_InfoPrintf("  External ram size: %s", StringConverter::SizeToHumanReadableString(m_external_ram_size).GetCharArray());
    return true;

}

bool Cartridge::Load(ByteStream *pStream, Error *pError)
{
    if (!ParseHeader(pStream, pError))
        return false;

    // back to start - cart header is part of the first rom bank
    if (!pStream->SeekAbsolute(0))
    {
        pError->SetErrorUser(1, "Failed to re-seek");
        return false;
    }

    // read rom banks
    for (uint32 i = 0; i < m_num_rom_banks; i++)
    {
        m_rom_banks[i] = (byte *)Y_malloc(ROM_BANK_SIZE);
        DebugAssert(m_rom_banks[i] != nullptr);
        if (!pStream->Read2(m_rom_banks[i], ROM_BANK_SIZE))
        {
            pError->SetErrorUserFormatted(1, "Failed to read ROM bank %u", i);
            return false;
        }
    }

    return true;
}

