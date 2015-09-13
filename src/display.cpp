#include "display.h"
#include "memory.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/String.h"
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

    // start with lcd on?
    //m_registers[DISPLAY_REG_LCDC] = 0xFF;

    // start at the end of vblank which is equal to starting fresh
    SetMode(2);
    SetScanline(0);
    m_modeClocksRemaining = 80;
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

uint8 Display::ReadTile(bool high_tileset, int32 tile, uint8 x, uint8 y) const
{
    // find the base address of this tile
    //uint16 BGTILEBASE = (high_tileset) ? 0x0800 : 0x0000;
    const byte *tilemem = m_system->GetVRAM();// + BGTILEBASE;
    if (high_tileset)
        tilemem += 0x800 + ((tile + 128) * 16);
    else
        tilemem += ((uint8)tile * 16);

    // 2 bytes represent a line, with the LSB on the even byte
    // and the MSB on the odd byte
    uint32 byteIndex = y * 2;
    uint32 bitIndex = x % 8;

    // extract 2-bit palette index
    byte colourBit = (tilemem[byteIndex] >> (7 - bitIndex)) & 0x1;
    colourBit |= ((tilemem[byteIndex + 1] >> (7 - bitIndex)) & 0x1) << 1;

    // palette index
    return colourBit & 0x3;
}

void Display::RenderScanline()
{
    const uint32 grayscale_colors[4] = { 0xFFFFFFFF, 0xFFC0C0C0, 0xFF606060, 0xFF000000 };
    //const uint32 grayscale_colors[4] = { 0xFF000000, 0xFF606060, 0xFFC0C0C0, 0xFFFFFFFF};

    // blank the line
    DebugAssert(m_currentScanLine < SCREEN_HEIGHT);
    byte *pFrameBufferLine = m_frameBuffer + (m_currentScanLine * SCREEN_WIDTH * 4);
    Y_memset(pFrameBufferLine, 0xFF, SCREEN_WIDTH * 4);

    // if display disabled, skip entirely (TODO Move this to a member variable, it shouldn't be modified outside of vsync)
    if (!(m_registers.LCDC & 0x80))
        return;

    // read control register
    uint8 LCDC = m_registers.LCDC;
    uint8 LINE = m_registers.LY;
    uint8 SCX = m_registers.SCX;
    uint8 SCY = m_registers.SCY;
    uint8 WX = m_registers.WX;
    uint8 WY = m_registers.WY;

    // parse control register
    const byte *VRAM = m_system->GetVRAM();
    uint8 BG_ENABLE = !!(LCDC & 0x01);
    //uint16 BGMAPBASE = (LCDC & 0x08) ? 0x1C00 : 0x1800;
    uint8 BG_TILEMAP = (LCDC >> 3) & 0x1;
    uint8 WINDOW_ENABLE = (LCDC >> 5) & 0x1;
    uint8 WINDOW_TILEMAP = (LCDC >> 6) & 0x1;
    uint8 BGTILESET_SELECT = !!(LCDC & 0x10); // bit 4
    uint8 SPRITE_SIZE_BIT = ((LCDC >> 2) & 0x1);
    uint8 SPRITE_HEIGHT = 8 + SPRITE_SIZE_BIT * 8; // bit 2
    uint8 SPRITE_ENABLE = !!(LCDC & 0x02);

    // read background palette
    uint32 background_palette[4] = { grayscale_colors[m_registers.BGP & 0x3], grayscale_colors[(m_registers.BGP >> 2) & 0x3], grayscale_colors[(m_registers.BGP >> 4) & 0x3], grayscale_colors[(m_registers.BGP >> 6) & 0x3] };

    // read sprite palettes
    uint32 obj_palette0[4] = { 0xFF555555, grayscale_colors[(m_registers.OBP0 >> 2) & 0x3], grayscale_colors[(m_registers.OBP0 >> 4) & 0x3], grayscale_colors[(m_registers.OBP0 >> 6) & 0x3] };
    uint32 obj_palette1[4] = { 0xFF555555, grayscale_colors[(m_registers.OBP1 >> 2) & 0x3], grayscale_colors[(m_registers.OBP1 >> 4) & 0x3], grayscale_colors[(m_registers.OBP1 >> 6) & 0x3] };

    // read sprites
    OAM_ENTRY active_sprites[40];
    uint32 num_active_sprites = 0;
    if (SPRITE_ENABLE)
    {
        // cull sprites
        for (uint32 i = 0; i < 40; i++)
        {
            // x/y in oam describes the bottom-right corner position (to position sprite at 0,0 it would be 8,16)
            const OAM_ENTRY *attributes = reinterpret_cast<const OAM_ENTRY *>(m_system->GetOAM()) + i;
            if (attributes->x == 0 || attributes->y == 0 || attributes->x >= 168 || attributes->y >= 160)       // offscreen
                continue;

            // translate to upper left/top, test if within our scanline
            int32 sprite_start_y = (int32)attributes->y - 16;
            int32 sprite_end_y = sprite_start_y + (int32)SPRITE_HEIGHT - 1;
            if ((int32)LINE < sprite_start_y || (int32)LINE > sprite_end_y)
                continue;

            // add to list
            active_sprites[num_active_sprites] = *attributes;
            num_active_sprites++;
        }

        // sort sprites
        if (num_active_sprites > 0)
        {
            Y_qsort(active_sprites, num_active_sprites, sizeof(active_sprites[0]), [](const void *a, const void *b) -> int
            {
                // non-cgb mode -> x coordinate determines priority
                OAM_ENTRY *oa = (OAM_ENTRY *)a;
                OAM_ENTRY *ob = (OAM_ENTRY *)b;
                if (oa->x < ob->x)
                    return -1;
                else if (oa->x > ob->x)
                    return 1;
                else
                    return 0;
            });

            // hardware can only draw 10 sprites, highest priority first
            num_active_sprites = Max(num_active_sprites, (uint32)10);
        }
    }

    // render the scanline
    for (uint32 pixel_x = 0; pixel_x < 160; pixel_x++)
    {
        uint32 color = 0xFFFFFFFF;
        uint8 bgcolor_index = 0;
        
        // background on?
        if (BG_ENABLE || WINDOW_ENABLE)
        {
            // scrolled coordinates
            int32 ix, iy;

            // inside window?
            uint8 tilemap;
            if (WINDOW_ENABLE && (int32)pixel_x >= ((int32)WX - 7) && LINE >= WY)
            {
                ix = (int32)pixel_x - ((int32)WX - 7);
                iy = (int32)LINE - (int32)WY;
                tilemap = WINDOW_TILEMAP;
            }
            else
            {
                // convert x,y to 256x256 tile system (tile is 8x8 each, 32 tiles each way)
                ix = (int32)pixel_x + (int32)SCX;
                iy = (int32)LINE + (int32)SCY;
                tilemap = BG_TILEMAP;

                // wrap around at edges
                ix %= 256;
                iy %= 256;
            }

            // find out which 8x8 tile this lies in
            int32 tilemapx = ix / 8;
            int32 tilemapy = iy / 8;
            int32 tilemapindex = tilemapy * 32 + tilemapx;

            // read the tile byte
            int8 tile;
            if (tilemap == 0)
                tile = (int8)VRAM[0x1800 + tilemapindex];
            else
                tile = (int8)VRAM[0x1C00 + tilemapindex];

            // read the tile pattern, access palette
            bgcolor_index = ReadTile((BGTILESET_SELECT == 0), tile, ix % 8, iy % 8);
            color = background_palette[bgcolor_index];
        }

        // sprites on?
        if (SPRITE_ENABLE)
        {
            // find the first sprite with an x range including this pixel
            for (uint32 sprite_index = 0; sprite_index < num_active_sprites; sprite_index++)
            {
                const OAM_ENTRY *sprite = &active_sprites[sprite_index];
                int32 sprite_start_x = (int32)sprite->x - 8;
                int32 sprite_end_x = sprite_start_x + 7;
                int32 sprite_start_y = (int32)sprite->y - 16;
                int32 sprite_end_y = sprite_start_y + (int32)SPRITE_HEIGHT - 1;
                if ((int32)pixel_x < sprite_start_x || (int32)pixel_x > sprite_end_x)
                    continue;

                // Y should already be in range
                DebugAssert((int32)LINE >= sprite_start_y && (int32)LINE <= sprite_end_y);

                // found a sprite! check the priority, priority1 = behind bg color 1-3
                if (sprite->priority == 1 && bgcolor_index != 0)
                    continue;

                // turn the x position and scanline into tile-space coordinates
                int32 tile_x = (int32)pixel_x - sprite_start_x;
                int32 tile_y = (int32)LINE - sprite_start_y;
                DebugAssert(tile_x >= 0 && tile_x < 16 && tile_y >= 0 && tile_y < (int32)SPRITE_HEIGHT);

                // "In 8x16 mode, the lower bit of the tile number is ignored. Ie. the upper 8x8 tile is "NN AND FEh", and the lower 8x8 tile is "NN OR 01h"."
                uint16 tile_index;
                if (SPRITE_SIZE_BIT)
                {
                    if (tile_y >= 8)
                    {
                        tile_index = sprite->tile | 0x01;
                        tile_y -= 8;
                    }
                    else
                    {
                        tile_index = (sprite->tile & 0xFE);
                    }
                }
                else
                {
                    tile_index = sprite->tile;
                }

                // handle flipped sprites
                if (sprite->hflip)
                    tile_x = 15 - tile_x;
                if (sprite->vflip)
                    tile_y = SPRITE_HEIGHT - tile_y;

                // get palette index
                uint8 palette_index = ReadTile(0, tile_index, (uint8)tile_x, (uint8)tile_y);
                if (palette_index == 0)
                {
                    // sprite colour 0 is transparent, try to draw other sprites instead.
                    continue;

                }

                // select sprite colour
                if (sprite->palette)
                    color = obj_palette1[palette_index];
                else
                    color = obj_palette0[palette_index];

                // don't render any other sprites on top of it
                break;
            }
        }

        // write pixel to framebufer
        PutPixel(pixel_x, LINE, color);
    }
}

void Display::DisplayTiles()
{
    const uint32 grayscale_colors[4] = { 0xFFFFFFFF, 0xFFC0C0C0, 0xFF606060, 0xFF000000 };

    uint32 draw_x = 0;
    uint32 draw_y = 0;

    Y_memzero(m_frameBuffer, sizeof(m_frameBuffer));

    for (uint32 tile = 0; tile < 192; tile++)
    {
        for (uint8 y = 0; y < 8; y++)
        {
            for (uint8 x = 0; x < 8; x++)
            {
                uint8 paletteidx;
                if (tile <= 64)
                    paletteidx = ReadTile(false, tile, x, y);
                else
                    paletteidx = ReadTile(true, -128 + (int32)(tile), x, y);

                PutPixel(draw_x + x, draw_y + y, grayscale_colors[paletteidx]);
            }
        }
        
        draw_x += 8;
        if (draw_x == 160)
        {
            draw_x = 0;
            draw_y += 8;
        }
    }
        
    m_system->CopyFrameBufferToSurface();
}
void Display::RenderFull()
{
    uint32 line = m_currentScanLine;
    

    for (uint32 y = 0; y < SCREEN_HEIGHT; y++)
    {
        m_currentScanLine = y;
        m_registers.LY = y & 0xff;
        RenderScanline();
    }

    m_currentScanLine = line;
    m_registers.LY = m_currentScanLine & 0xff;

    m_system->CopyFrameBufferToSurface();

}


void Display::DumpTiles()
{
    uint8 LCDC = m_registers.LCDC;
    const byte *VRAM = m_system->GetVRAM();
    uint8 BG_ENABLE = !!(LCDC & 0x01);
    uint16 BGMAPBASE = (LCDC & 0x08) ? 0x1C00 : 0x1800;

    if (BG_ENABLE)
    {
        String msg;

        for (uint32 gy = 0; gy < 32; gy++)
        {
            msg.Clear();
            msg.AppendFormattedString("Group %u: ", gy);
            for (uint32 gx = 0; gx < 32; gx++)
            {
                int8 id = (int8)VRAM[BGMAPBASE + gy * 32 + gx];
                msg.AppendFormattedString("%d, ", id);
            }
            Log_DevPrint(msg);
        }
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
