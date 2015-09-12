#pragma once
#include "YBaseLib/Common.h"
#include "YBaseLib/Assert.h"
#include "system.h"

class Display
{
public:
    static const uint32 SCREEN_WIDTH = 160;
    static const uint32 SCREEN_HEIGHT = 144;

    struct Registers
    {
        union
        {
            struct  
            {
                uint8 LCDC;
                uint8 STAT;
                uint8 SCY;
                uint8 SCX;
                uint8 LY;
                uint8 LYC;
                uint8 DMA;
                uint8 BGP;
                uint8 OBP0;
                uint8 OBP1;
                uint8 WY;
                uint8 WX;
            };
            uint8 regs[NUM_DISPLAY_REGS];
        };
    };

public:
    Display(System *system);
    ~Display();

    const byte *GetFrameBuffer() const { return m_frameBuffer; }

    // register access
    const uint8 GetRegister(DISPLAY_REG reg) { DebugAssert(reg < NUM_DISPLAY_REGS); return m_registers.regs[reg]; }
    const uint8 GetRegisterControl() const { return m_registers.regs[DISPLAY_REG_LCDC]; }
    const uint8 GetRegisterStatus() const { return m_registers.regs[DISPLAY_REG_STAT]; }
    const uint8 GetRegisterScrollY() const { return m_registers.regs[DISPLAY_REG_SCY]; }
    const uint8 GetRegisterScrollX() const { return m_registers.regs[DISPLAY_REG_SCX]; }
    const uint8 GetRegisterCurrentScanline() const { return m_registers.regs[DISPLAY_REG_LY]; }

    // register writes
    void SetRegister(DISPLAY_REG reg, uint8 value) { /* todo: read-only registers */ DebugAssert(reg < NUM_DISPLAY_REGS); m_registers.regs[reg] = value; }
    void SetRegisterControl(uint8 value) { m_registers.regs[DISPLAY_REG_LCDC] = value; }
    void SetRegisterStatus(uint8 value) { m_registers.regs[DISPLAY_REG_STAT] = value; }
    void SetRegisterScrollY(uint8 value) { m_registers.regs[DISPLAY_REG_SCY] = value; }
    void SetRegisterScrollX(uint8 value) { m_registers.regs[DISPLAY_REG_SCX] = value; }
    void SetRegisterCurrentScanline(uint8 value) { m_registers.regs[DISPLAY_REG_LY] = value; }

    // reset
    void Reset();

    // step
    bool Step();

private:
    void RenderScanline();

    void SetMode(uint32 mode);
    void SetScanline(uint32 scanline);

    void PutPixel(uint32 x, uint32 y, uint32 color);

    // returns index into palette
    uint8 ReadTile(bool high_tileset, uint16 tile, uint8 x, uint8 y) const;

    System *m_system;

    // registers - use a struct here?
    Registers m_registers;

    // copies of memory
    byte m_vramCopy[0x2000];
    OAM_ENTRY m_oamCopy[40];

    // state
    uint32 m_mode;
    uint32 m_modeClocksRemaining;
    uint32 m_currentScanLine;
    
    byte m_frameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT * 4];   // RGBA
};

