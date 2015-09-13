#pragma once
#include "YBaseLib/Common.h"

const uint32 GB_BIOS_LENGTH = 256;

#pragma pack(push, 1)
struct OAM_ENTRY
{
    byte y;
    byte x;
    byte tile;
    byte __unused : 4;
    byte palette : 1;
    byte hflip : 1;
    byte vflip : 1;
    byte priority : 1;
};
#pragma pack(pop)

enum CPU_IRQ
{
    CPU_INT_VBLANK,
    CPU_INT_LCDSTAT,
    CPU_INT_TIMER,
    CPU_INT_SERIAL,
    CPU_INT_JOYPAD,
    NUM_CPU_INT
};

// http://bgb.bircd.org/pandocs.htm#videodisplay
enum DISPLAY_REG
{
    // Range of 0xFF40-0xFF4B       // CPU Access
    DISPLAY_REG_LCDC = 0x00,        // R/W
    DISPLAY_REG_STAT = 0x01,        // R/W
    DISPLAY_REG_SCY = 0x02,         // R/W
    DISPLAY_REG_SCX = 0x03,         // R/W
    DISPLAY_REG_LY = 0x04,          // R
    DISPLAY_REG_LYC = 0x05,         // R/W
    DISPLAY_REG_DMA = 0x06,         // W
    DISPLAY_REG_WY = 0x0A,          // R/W
    DISPLAY_REG_WX = 0x0B,          // R/W
    DISPLAY_REG_BGP = 0x07,         // R/W - non-cgb mode only
    DISPLAY_REG_OBP0 = 0x08,        // R/W - non-cgb mode only
    DISPLAY_REG_OBP1 = 0x09,        // R/W - non-cgb mode only

    NUM_DISPLAY_REGS = 0x0C
};

enum DISPLAY_CGBREG
{
    // Range of 0xFF60
    //DISPLAY_CGBREG_
};

enum PAD_DIRECTION
{
    PAD_DIRECTION_NONE          = 0x00,
    PAD_DIRECTION_RIGHT         = 0x01,
    PAD_DIRECTION_LEFT          = 0x02,
    PAD_DIRECTION_UP            = 0x04,
    PAD_DIRECTION_DOWN          = 0x08,
    PAD_DIRECTION_MASK          = 0x0F
};

enum PAD_BUTTON
{
    PAD_BUTTON_A                = 0x01,
    PAD_BUTTON_B                = 0x02,
    PAD_BUTTON_SELECT           = 0x04,
    PAD_BUTTON_START            = 0x08,
    PAD_BUTTON_MASK             = 0x0F
};

#pragma pack(push, 1)
struct CART_HEADER
{
    uint8 entrypoint[4];
    uint8 logo[48];
    union
    {
        char title[16];
        struct
        {
            char cgb_title[11];
            char cgb_manufacturer[4];
            uint8 cgb_flag;
        };
    };
    uint8 cgb_licensee_code[2];
    uint8 sgb_flag;
    uint8 type;
    uint8 rom_size;
    uint8 ram_size;
    uint8 region_code;
    uint8 licensee_code;
    uint8 rom_version;
    uint8 header_checksum;
    uint16 cartridge_checksum;
};
#pragma pack(pop)

#define CART_HEADER_OFFSET (0x0100)
