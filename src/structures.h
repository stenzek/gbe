#pragma once
#include "YBaseLib/Common.h"

const uint32 GB_BIOS_LENGTH = 256;

#pragma pack(push, 1)
struct OAM_ENTRY
{
    byte y;
    byte x;
    byte tile;
    byte priority : 1;
    byte vflip : 1;
    byte hflip : 1;
    byte palette : 1;
    byte __unused : 4;
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
