#pragma once
#include "YBaseLib/Common.h"
#include "YBaseLib/Assert.h"
#include "system.h"

class ByteStream;
class BinaryReader;
class BinaryWriter;
class Error;

class Display
{
    friend System;

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

        // CGB
        uint8 HDMA1;
        uint8 HDMA2;
        uint8 HDMA3;
        uint8 HDMA4;
        uint8 HDMA5;
        uint8 BGPI;
        uint8 OBPI;
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
    void Synchronize();

private:
    void RenderScanline(uint8 LINE);
    void RenderScanline_CGB(uint8 LINE);
    void RenderFull();
    void DumpTiles(uint8 tilemap);
    void DisplayTiles();
    void PushFrame();

    void SetState(DISPLAY_STATE state);
    void SetLCDCRegister(uint8 value);
    void SetLYRegister(uint8 value);
    void SetHDMA5Register(uint8 value);

    // helper for oam bug
    bool CanTriggerOAMBug() const;

    // state saving
    bool LoadState(ByteStream *pStream, BinaryReader &binaryReader, Error *pError);
    void SaveState(ByteStream *pStream, BinaryWriter &binaryWriter);

    // framebuffer ops
    void ClearFrameBuffer();
    void PutPixel(uint32 x, uint32 y, uint32 color);

    // returns index into palette
    uint8 ReadTile(uint8 bank, bool high_tileset, int32 tile, uint8 x, uint8 y) const;
    uint32 ReadCGBPalette(const uint8 *palette, uint8 palette_index, uint8 color_index) const;

    // HDMA transfer
    void ExecuteHDMATransferBlock(uint32 bytes);

    System *m_system;
    uint32 m_last_cycle;

    // registers - use a struct here?
    Registers m_registers;

    // CGB palette
    uint8 m_cgb_bg_palette[64];
    uint8 m_cgb_sprite_palette[64];

    // state
    DISPLAY_STATE m_state;
    uint32 m_modeClocksRemaining;
    uint32 m_HDMATransferClocksRemaining;
    uint32 m_cyclesSinceVBlank;
    uint8 m_currentScanLine;
    
    byte m_frameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT * 4];   // RGBA
    bool m_frameReady;
};

