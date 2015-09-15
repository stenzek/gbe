#include "display.h"
#include "memory.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/String.h"
Log_SetChannel(Display);

Display::Display(System *memory)
    : m_system(memory)
    , m_frameReady(false)
{
    Reset();
}

Display::~Display()
{

}

uint8 Display::CPUReadRegister(uint8 index) const
{
    switch (index)
    {
    case DISPLAY_REG_LCDC:
        return m_registers.LCDC;
    case DISPLAY_REG_STAT:
        return m_registers.STAT;
    case DISPLAY_REG_SCY:
        return m_registers.SCY;
    case DISPLAY_REG_SCX:
        return m_registers.SCX;
    case DISPLAY_REG_LY:
        return m_registers.LY;
    case DISPLAY_REG_LYC:
        return m_registers.LYC;
    case DISPLAY_REG_WY:
        return m_registers.WY;
    case DISPLAY_REG_WX:
        return m_registers.WX;
    case DISPLAY_REG_BGP:
        return m_registers.BGP;
    case DISPLAY_REG_OBP0:
        return m_registers.OBP0;
    case DISPLAY_REG_OBP1:
        return m_registers.OBP1;
    }

    Log_WarningPrintf("Unhandled LCD register read: %02X", index);
    return 0x00;
}

void Display::CPUWriteRegister(uint8 index, uint8 value)
{
    switch (index)
    {
    case DISPLAY_REG_LCDC:
        SetLCDCRegister(value);
        return;
    case DISPLAY_REG_STAT:
        m_registers.STAT = (m_registers.STAT & ~0x78) | (value & 0x78);
        return;
    case DISPLAY_REG_SCY:
        m_registers.SCY = value;
        return;
    case DISPLAY_REG_SCX:
        m_registers.SCX = value;
        return;
    case DISPLAY_REG_LY:
        SetLYRegister(value);
        return;
    case DISPLAY_REG_LYC:
        m_registers.LYC = value;
        return;
    case DISPLAY_REG_WY:
        m_registers.WY = value;
        return;
    case DISPLAY_REG_WX:
        m_registers.WX = value;
        return;
    case DISPLAY_REG_BGP:
        m_registers.BGP = value;
        return;
    case DISPLAY_REG_OBP0:
        m_registers.OBP0 = value;
        return;
    case DISPLAY_REG_OBP1:
        m_registers.OBP1 = value;
        return;

    }

    Log_WarningPrintf("Unhandled LCD register write: %02X (value %02X)", index, value);
}

void Display::Reset()
{
    ClearFrameBuffer();
    m_frameReady = false;

    Y_memzero(&m_registers, sizeof(m_registers));

    // start at the end of vblank which is equal to starting fresh
    m_modeClocksRemaining = 0;
    m_cyclesSinceVBlank = 0;
    m_currentScanLine = 0;
    SetState(DISPLAY_STATE_OAM_READ);
    SetLYRegister(0);
    PushFrame();
}

void Display::SetState(DISPLAY_STATE state)
{
    m_state = state;

    // update lower two bits of STAT
    m_registers.STAT = (m_registers.STAT & ~0x3) | (m_state & 0x3);

    // trigger interrupts
    switch (state)
    {
        // HBlank
    case DISPLAY_STATE_HBLANK:
        {
            // HBlank lasts 201-207 clocks
            m_system->SetOAMLock(false);
            m_system->SetVRAMLock(false);
            m_modeClocksRemaining = 204;
            m_frameReady = false;

            // Fire interrupt
            if (m_registers.STAT & (1 << 3))
                m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);

            break;
        }

        // VBlank
    case DISPLAY_STATE_VBLANK:
        {
            // VBlank lasts for a total of 4560 clocks.
            // This is broken up into 10 scanlines of 456 clocks.
            m_system->SetOAMLock(false);
            m_system->SetVRAMLock(false);
            m_modeClocksRemaining = 456;
            m_frameReady = true;
            PushFrame();

            // Fire interrupts
            if (m_registers.STAT & (1 << 4))
                m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);

            // VBlank interrupt always fires.
            m_system->CPUInterruptRequest(CPU_INT_VBLANK);
            break;
        }

        // OAM
    case DISPLAY_STATE_OAM_READ:
        {
            // Lock the CPU from reading OAM.
            // Mode 2 read takes 77-83 clocks.
            m_system->SetOAMLock(true);
            m_system->SetVRAMLock(false);
            m_modeClocksRemaining = 80;
            m_frameReady = false;

            // Fire interrupts
            if (m_registers.STAT & (1 << 5))
                m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);

            break;
        }

        // VRAM read
    case DISPLAY_STATE_OAM_VRAM_READ:
        {
            // Lock the CPU from reading both OAM and VRAM.
            // Mode 3 takes 169-175 clocks.
            m_system->SetOAMLock(true);
            m_system->SetVRAMLock(true);
            m_modeClocksRemaining = 172;
            m_frameReady = false;
            break;
        }
    }
}

void Display::SetLCDCRegister(uint8 value)
{
    // handle switching LCD on/off
    bool lcd_current_state = !!(m_registers.LCDC >> 7);
    bool lcd_new_state = !!(m_registers.LCDC >> 7);
    if (lcd_current_state != lcd_new_state)
    {
        // Turning off LCD?
        if (!lcd_new_state)
        {
            // We should be in vblank.
            if (m_state != DISPLAY_STATE_VBLANK)
                Log_WarningPrintf("LCD display turned off whilst out of vblank state. This may damage a real DMG.");

            // Unlock memory.
            m_system->SetVRAMLock(false);
            m_system->SetOAMLock(false);

            // Clear the framebuffer, and update display.
            m_frameReady = true;
            ClearFrameBuffer();
            PushFrame();
        }
        else
        {
            // Reset back to original state (is this correct behavior?)
            m_currentScanLine = 0;
            SetState(DISPLAY_STATE_OAM_READ);
            SetLYRegister(0);
        }
    }

    // update register
    m_registers.LCDC = value;
}

void Display::SetLYRegister(uint8 value)
{
    m_registers.LY = value;

    // update coincidence flag
    uint8 coincidence_flag = (uint8)(m_registers.LYC == m_registers.LY);
    m_registers.STAT = (m_registers.STAT & ~(1 << 2)) | (coincidence_flag << 2);

    // coincidence interrupts
    if (coincidence_flag && m_registers.STAT & (1 << 6))
        m_system->CPUInterruptRequest(CPU_INT_LCDSTAT);    
}

void Display::ExecuteFor(uint32 cpuCycles)
{
    // Don't do anything if we're disabled.
    if (!(m_registers.LCDC & (1 << 7)))
        return;

    // Execute as much time as we can.
    while (cpuCycles > 0)
    {
        // Execute these many GPU cycles
        if (cpuCycles < m_modeClocksRemaining)
        {
            // Still has to wait.
            m_modeClocksRemaining -= cpuCycles;
            m_cyclesSinceVBlank += cpuCycles;
            break;
        }

        // Completed this wait period.
        cpuCycles -= m_modeClocksRemaining;
        m_cyclesSinceVBlank += m_modeClocksRemaining;
        
        // Switch to the next mode (if appropriate)
        switch (m_state)
        {
        case DISPLAY_STATE_OAM_READ:
            {
                // Enter OAM+VRAM read mode for this scanline.
                SetState(DISPLAY_STATE_OAM_VRAM_READ);
                break;
            }

        case DISPLAY_STATE_OAM_VRAM_READ:
            {
                // Render this scanline.
                RenderScanline(m_currentScanLine);

                // Enter HBLANK for this scanline
                SetState(DISPLAY_STATE_HBLANK);
                break;
            }

        case DISPLAY_STATE_HBLANK:
            {
                // Move to the next scanline
                m_currentScanLine++;
                SetLYRegister(m_registers.LY + 1);

                // Is this the last visible scanline?
                if (m_currentScanLine != 144)
                {
                    // Switch back to OAM read for this scaline.
                    SetState(DISPLAY_STATE_OAM_READ);
                }
                else
                {
                    // Move to VBLANK.
                    SetState(DISPLAY_STATE_VBLANK);
                }

                break;
            }

        case DISPLAY_STATE_VBLANK:
            {
                // Is this the last out-of-range scanline?
                if (m_currentScanLine == 153)
                {
                    // Check cycle counter
                    DebugAssert(m_cyclesSinceVBlank == 70224);

                    // Next frame.
                    m_frameReady = false;
                    m_currentScanLine = 0;
                    m_cyclesSinceVBlank = 0;
                    SetState(DISPLAY_STATE_OAM_READ);
                    SetLYRegister(0);
                }
                else
                {
                    // Move to the next out-of-range scanline
                    m_modeClocksRemaining = 456;
                    m_currentScanLine++;
                    SetLYRegister(m_registers.LY + 1);
                }

                break;
            }
        }
    }
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

void Display::RenderScanline(uint8 LINE)
{
    const uint32 grayscale_colors[4] = { 0xFFFFFFFF, 0xFFC0C0C0, 0xFF606060, 0xFF000000 };
    //const uint32 grayscale_colors[4] = { 0xFF000000, 0xFF606060, 0xFFC0C0C0, 0xFFFFFFFF};

    // blank the line
    byte *pFrameBufferLine = m_frameBuffer + (LINE * SCREEN_WIDTH * 4);
    Y_memset(pFrameBufferLine, 0xFF, SCREEN_WIDTH * 4);

    // if display disabled, skip entirely (TODO Move this to a member variable, it shouldn't be modified outside of vsync)
    if (!(m_registers.LCDC & 0x80))
        return;

    // read control register
    uint8 LCDC = m_registers.LCDC;
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
            num_active_sprites = Min(num_active_sprites, (uint32)10);
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

void Display::ClearFrameBuffer()
{
    Y_memset(m_frameBuffer, 0xFF, sizeof(m_frameBuffer));
}

void Display::PutPixel(uint32 x, uint32 y, uint32 color)
{
    byte *base = m_frameBuffer + (y * SCREEN_WIDTH + x) * 4;
    base[0] = color & 0xFF;
    base[1] = (color >> 8) & 0xFF;
    base[2] = (color >> 16) & 0xFF;
    base[3] = (color >> 24) & 0xFF;
}

void Display::PushFrame()
{
    if (m_system->m_callbacks != nullptr)
        m_system->m_callbacks->PresentDisplayBuffer(m_frameBuffer, SCREEN_WIDTH * 4);
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
        
    PushFrame();
}

void Display::RenderFull()
{
    for (uint32 y = 0; y < SCREEN_HEIGHT; y++)
        RenderScanline((uint8)y);

    PushFrame();
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
