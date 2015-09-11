#include "display.h"
#include "memory.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/Log.h"
Log_SetChannel(Display);

Display::Display(System *memory)
    : m_system(memory)
{
    Reset();
}

Display::~Display()
{

}

void Display::Reset()
{
    Y_memset(m_frameBuffer, 0xFF, sizeof(m_frameBuffer));
    Y_memzero(&m_registers, sizeof(m_registers));
    Y_memzero(m_vramCopy, sizeof(m_vramCopy));
    Y_memzero(m_oamCopy, sizeof(m_oamCopy));

    // start with lcd on?
    //m_registers[DISPLAY_REG_LCDC] = 0xFF;

    // start at the end of vblank which is equal to starting fresh
    SetMode(2);
    m_modeClocksRemaining = 80;
    m_currentScanLine = 0;
}

void Display::SetMode(uint32 mode)
{
    m_mode = mode;

    // update lower two bits of STAT
    m_registers.STAT = (m_registers.STAT & ~0x3) | (m_mode & 0x3);

    // trigger interrupts
    switch (mode)
    {
        // HBlank
    case 0:
        {
            if (m_registers.STAT & (1 << 3))
                m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);

            break;
        }

        // VBlank
    case 1:
        {
            if (m_registers.STAT & (1 << 4))
                m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);

            m_system->CPUInterruptRequest(CPU_INT_VBLANK);
            break;
        }

        // OAM
    case 2:
        {
            if (m_registers.STAT & (1 << 5))
                m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);

            break;
        }
    }
}


void Display::SetScanline(uint32 scanline)
{
    m_currentScanLine = scanline;
    m_registers.LY = scanline & 0xFF;

    // update coincidence flag
    uint8 coincidence_flag;
    if (m_registers.STAT & (1 << 2))
        coincidence_flag = (m_registers.LYC == m_registers.LY) ? 1 : 0;
    else
        coincidence_flag = (m_registers.LYC != m_registers.LY) ? 1 : 0;
    m_registers.STAT = (m_registers.STAT & ~(1 << 2)) | (coincidence_flag << 2);

    // coindcince interrupts
    if (coincidence_flag && m_registers.STAT & (1 << 6))
        m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);    
}

bool Display::Step()
{
    bool pushFrameBuffer = false;

    switch (m_mode)
    {
        // oam read mode
    case 2:
        {
            DebugAssert(m_modeClocksRemaining > 0);
            m_modeClocksRemaining--;
            if (m_modeClocksRemaining == 0)
            {
                // read sprites
                Y_memcpy(m_oamCopy, m_system->GetOAM(), sizeof(m_oamCopy));

                // enter scanline mode 3 for 172 clocks
                m_modeClocksRemaining = 172;
                SetMode(3);
            }
        }
        break;

        // vram read mode
    case 3:
        {
            DebugAssert(m_modeClocksRemaining > 0);
            m_modeClocksRemaining--;
            if (m_modeClocksRemaining == 0)
            {
                // read vram
                Y_memcpy(m_vramCopy, m_system->GetVRAM(), sizeof(m_vramCopy));

                // enter hblank
                SetMode(0);
                m_modeClocksRemaining = 204;

                // render the scanline
                RenderScanline();
            }
        }
        break;

        // hblank
    case 0:
        {
            DebugAssert(m_modeClocksRemaining > 0);
            m_modeClocksRemaining--;
            if (m_modeClocksRemaining == 0)
            {
                // move to the next line
                SetScanline(m_currentScanLine + 1);
                if (m_currentScanLine == 144)
                {
                    // enter vblank
                    SetMode(1);
                    m_modeClocksRemaining = 456;

                    // write framebuffer
                    pushFrameBuffer = true;
                }
                else
                {
                    // mode to oam for next line
                    SetMode(2);
                    m_modeClocksRemaining = 80;
                }
            }
        }
        break;

        // vblank
    case 1:
        {
            DebugAssert(m_modeClocksRemaining > 0);
            m_modeClocksRemaining--;
            if (m_modeClocksRemaining == 0)
            {
                // move to next line
                SetScanline(m_currentScanLine + 1);
                if (m_currentScanLine == 154)
                {
                    // return back to oam for first line
                    SetMode(2);
                    SetScanline(0);
                    m_modeClocksRemaining = 80;
                }
                else
                {
                    // still in vblank
                    m_modeClocksRemaining = 456;
                }
            }
        }
        break;
    }

    
    /*static uint32 i = 0;
    if (i != m_currentScanLine)
    {
        i = m_currentScanLine;
        Log_DevPrintf("scan %u", i);
    }*/

    return pushFrameBuffer;
}

void Display::RenderScanline()
{
    // read background palette
    const uint32 grayscale_colors[4] = { 0xFFFFFFFF, 0xFFC0C0C0, 0xFF606060, 0xFF000000 };
    //const uint32 grayscale_colors[4] = { 0xFF000000, 0xFF606060, 0xFFC0C0C0, 0xFFFFFFFF};
    uint32 background_palette[4] =
    {
        grayscale_colors[m_registers.BGP & 0x3],
        grayscale_colors[(m_registers.BGP >> 2) & 0x3],
        grayscale_colors[(m_registers.BGP >> 4) & 0x3],
        grayscale_colors[(m_registers.BGP >> 6) & 0x3],
    };

    // blank the line
    DebugAssert(m_currentScanLine < SCREEN_HEIGHT);
    byte *pFrameBufferLine = m_frameBuffer + (m_currentScanLine * SCREEN_WIDTH * 4);
    Y_memset(pFrameBufferLine, 0xFF, SCREEN_WIDTH * 4);

    // read control register
    uint8 LCDC = m_registers.LCDC;
    uint8 LINE = m_registers.LY;

    // lcd on?
    if (LCDC & 0x80)
    {
        const byte *VRAM = m_vramCopy;

        // background on?
        if (LCDC & 0x01)
        {
            uint8 SCX = m_registers.SCX;
            uint8 SCY = m_registers.SCY;
            uint16 BGMAPBASE = (LCDC & 0x08) ? 0x1C00 : 0x1800;
            uint16 BGMAPOFFSET = (((LINE + SCY) & 255) >> 3) << 5;

            byte lineOffset = SCX >> 3;

            byte x = SCX & 7;
            byte y = (LINE + SCY) & 7;

            // get tile index
            uint16 BGTILEBASE = (LCDC & 0x10) ? 0x0000 : 0x0800;
            uint16 tile = VRAM[BGMAPBASE + BGMAPOFFSET + lineOffset];

            // 20 tiles of 8x8 = 160 wide
            for (uint32 i = 0; i < 160; i++)
            {
                //const byte *tilemem = VRAM + ((8 * 8) * tile);
                //tilemem += (y * 8) + x;
                //byte color = *tilemem;
                //PutPixel(i, m_currentScanLine, (color != 0) ? 0xFFFFFFFF : 0);

                // find the base address of this tile
                const byte *tilemem = VRAM + BGTILEBASE + (tile * 16);

                // 2 bytes represent a line, with the LSB on the even byte
                // and the MSG on the odd byte
                uint32 byteIndex = y * 2;
                uint32 bitIndex = x % 8;

                // extract 2-bit colour
                byte colourBit = (tilemem[byteIndex] >> (7 - bitIndex)) & 0x1;
                colourBit |= ((tilemem[byteIndex + 1] >> (7 - bitIndex)) & 0x1) << 1;

                uint32 color = background_palette[colourBit & 0x3];
                PutPixel(i, m_currentScanLine, color);

                x++;
                if (x == 8)
                {
                    x = 0;
                    lineOffset = (lineOffset + 1) & 31;
                    tile = VRAM[BGMAPBASE + BGMAPOFFSET + lineOffset];
                }
            }
        }

//         // sprites on?
//         if (CR & 0x02)
//         {
//             for (uint32 i = 0; i < 40; i++)
//             {
//                 OAM_ENTRY sprite = m_pMMU->GetOAMEntry(i);
//                 
//                 int sx = (int)sprite.x - 8;
//                 int sy = (int)sprite.y - 16;
// 
//                 if (sy <= (int)m_currentScanLine && (sy + 8) >(int)m_currentScanLine)
//                 {
// 
//                 }
//             }
//         }
    }
}

void Display::PutPixel(uint32 x, uint32 y, uint32 color)
{
    byte *base = m_frameBuffer + (y * SCREEN_WIDTH + x) * 4;
    base[0] = color & 0xFF;
    base[1] = (color >> 8) & 0xFF;
    base[2] = (color >> 16) & 0xFF;
    base[3] = (color >> 24) & 0xFF;
}

