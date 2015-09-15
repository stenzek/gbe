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
        uint8 LCDC;
        uint8 STAT;
        uint8 SCY;
        uint8 SCX;
        uint8 LY;
        uint8 LYC;
        uint8 BGP;
        uint8 OBP0;
        uint8 OBP1;
        uint8 WY;
        uint8 WX;
    };

public:
    Display(System *system);
    ~Display();

    const byte *GetFrameBuffer() const { return m_frameBuffer; }
    const bool GetFrameReady() const { return m_frameReady; }
    void ClearFrameReady() { m_frameReady = false; }

    // current scanline access
    const uint32 GetCurrentScanLine() const { return m_currentScanLine; }

    // register access
    uint8 CPUReadRegister(uint8 index) const;
    void CPUWriteRegister(uint8 index, uint8 value);

    // reset
    void Reset();

    // step
    void ExecuteFor(uint32 cpuCycles);

private:
    void RenderScanline(uint8 LINE);
    void RenderFull();
    void DumpTiles();
    void DisplayTiles();

    void SetState(DISPLAY_STATE state);
    void SetLYRegister(uint8 value);

    void PutPixel(uint32 x, uint32 y, uint32 color);

    // returns index into palette
    uint8 ReadTile(bool high_tileset, int32 tile, uint8 x, uint8 y) const;

    System *m_system;

    // registers - use a struct here?
    Registers m_registers;

    // state
    DISPLAY_STATE m_state;
    uint32 m_modeClocksRemaining;
    uint32 m_cyclesSinceVBlank;
    uint8 m_currentScanLine;
    
    byte m_frameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT * 4];   // RGBA
    bool m_frameReady;
};

