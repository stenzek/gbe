#include "system.h"
#include "cartridge.h"
#include "cpu.h"
#include "display.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include <SDL/SDL.h>
Log_SetChannel(System);

System::System()
{
    m_cpu = new CPU(this);
    m_display = new Display(this);
    m_window = nullptr;
    m_surface = nullptr;
    m_cartridge = nullptr;
    m_bios = nullptr;
    m_biosLatch = false;
    m_vramLocked = false;
    m_oamLocked = false;
}

System::~System()
{
    delete m_display;
    delete m_cpu;
}

bool System::Init(const byte *bios, Cartridge *cartridge)
{
    m_bios = bios;
    m_cartridge = cartridge;
    return true;
}

void System::Reset()
{
    m_cpu->Reset();
    m_display->Reset();
    ResetMemory();
    ResetTimer();
    ResetPad();
    if (m_cartridge != nullptr)
        m_cartridge->Reset();

    // if bios not provided, emulate post-bootstrap state
    if (m_bios == nullptr)
        SetPostBootstrapState();

    CopyFrameBufferToSurface();
}

void System::Step()
{
    uint32 cycles = m_cpu->Step();
    for (uint32 i = 0; i < cycles / 4; i++)
    {
        // TODO: Pass cycles as argument
        if (m_display->Step())
        {
            CopyFrameBufferToSurface();
            //Sleep(16);
        }
    }

    UpdateTimer(cycles);
}

void System::SetPadDirection(PAD_DIRECTION direction)
{
    uint8 old_direction_state = m_pad_direction_state;
    m_pad_direction_state = ~(PAD_DIRECTION_MASK & direction) & PAD_DIRECTION_MASK;
    if (old_direction_state != m_pad_direction_state)
    {
        Log_DevPrintf("Pad direction set to 0x%02X", direction);
        CPUInterruptRequest(CPU_INT_JOYPAD);
    }
}

void System::SetPadDirection(PAD_DIRECTION direction, bool state)
{
    uint8 old_direction_state = m_pad_direction_state;

    if (!state)
        m_pad_direction_state |= 0xF & direction;
    else
        m_pad_direction_state &= ~(0xF & direction);

    if (old_direction_state != m_pad_direction_state)
    {
        Log_DevPrintf("Pad direction 0x%02X set %s", direction, state ? "on" : "off");
        CPUInterruptRequest(CPU_INT_JOYPAD);
    }
}

void System::SetPadButton(PAD_BUTTON button, bool state)
{
    uint8 old_button_state = m_pad_button_state;

    if (!state)
        m_pad_button_state |= 0xF & button;
    else
        m_pad_button_state &= ~(0xF & button);

    if (old_button_state != m_pad_button_state)
    {
        Log_DevPrintf("Pad button 0x%02X set %s", button, state ? "on" : "off");
        CPUInterruptRequest(CPU_INT_JOYPAD);
    }
}

void System::DMATransfer(uint16 source_address, uint16 destination_address, uint32 bytes)
{
    // slow but due to the ranges has to be done this awy
    uint16 current_source_address = source_address;
    uint16 current_destination_address = destination_address;
    for (uint32 i = 0; i < bytes; i++, current_source_address++, current_destination_address++)
        CPUWrite(current_destination_address, CPURead(current_source_address));

    // TODO: Stall memory access for 160 cycles
}

void System::ResetMemory()
{
    // bios is initially mapped
    m_biosLatch = true;

    // zero all memory
    Y_memzero(m_memory_vram, sizeof(m_memory_vram));
    Y_memzero(m_memory_wram, sizeof(m_memory_wram));
    Y_memzero(m_memory_oam, sizeof(m_memory_oam));
    Y_memzero(m_memory_zram, sizeof(m_memory_zram));

    // pad
    m_pad_row_select = 0;
}

void System::ResetTimer()
{
    m_timer_clocks = 0;
    m_timer_divider_clocks = 0;
    m_timer_divider = 1;
    m_timer_counter = 0;
    m_timer_overflow_value = 0;
    m_timer_control = 0;
}

void System::ResetPad()
{
    m_pad_row_select = 0x30;        // neither selected
    m_pad_direction_state = 0x0F;   // nothing down
    m_pad_button_state = 0x0F;      // nothing down
}

void System::SetPostBootstrapState()
{
    // http://bgb.bircd.org/pandocs.txt -> Power Up Sequence
    CPU::Registers *registers = m_cpu->GetRegisters();
    registers->AF = 0x01B0;
    registers->BC = 0x0013;
    registers->DE = 0x00D8;
    registers->HL = 0x014D;
    registers->SP = 0xFFFE;
    registers->PC = 0x0100;

    CPUWriteIORegister(0x05, 0x00);   // TIMA
    CPUWriteIORegister(0x06, 0x00);   // TMA
    CPUWriteIORegister(0x07, 0x00);   // TAC
    CPUWriteIORegister(0x10, 0x80);   // NR10
    CPUWriteIORegister(0x11, 0xBF);   // NR11
    CPUWriteIORegister(0x12, 0xF3);   // NR12
    CPUWriteIORegister(0x14, 0xBF);   // NR14
    CPUWriteIORegister(0x16, 0x3F);   // NR21
    CPUWriteIORegister(0x17, 0x00);   // NR22
    CPUWriteIORegister(0x19, 0xBF);   // NR24
    CPUWriteIORegister(0x1A, 0x7F);   // NR30
    CPUWriteIORegister(0x1B, 0xFF);   // NR31
    CPUWriteIORegister(0x1C, 0x9F);   // NR32
    CPUWriteIORegister(0x1E, 0xBF);   // NR33
    CPUWriteIORegister(0x20, 0xFF);   // NR41
    CPUWriteIORegister(0x21, 0x00);   // NR42
    CPUWriteIORegister(0x22, 0x00);   // NR43
    CPUWriteIORegister(0x23, 0xBF);   // NR30
    CPUWriteIORegister(0x24, 0x77);   // NR50
    CPUWriteIORegister(0x25, 0xF3);   // NR51
    CPUWriteIORegister(0x26, 0xF1);     // NR52 (F0 on SGB)
    CPUWriteIORegister(0x40, 0x91);   // LCDC
    CPUWriteIORegister(0x42, 0x00);   // SCY
    CPUWriteIORegister(0x43, 0x00);   // SCX
    CPUWriteIORegister(0x45, 0x00);   // LYC
    CPUWriteIORegister(0x47, 0xFC);   // BGP
    CPUWriteIORegister(0x48, 0xFF);   // OBP0
    CPUWriteIORegister(0x49, 0xFF);   // OBP1
    CPUWriteIORegister(0x4A, 0x00);   // WY
    CPUWriteIORegister(0x4B, 0x00);   // WX
    CPUWriteIORegister(0xFF, 0x00);   // IE

    m_biosLatch = false;
}

void System::CopyFrameBufferToSurface()
{
    const byte *frameBuffer = m_display->GetFrameBuffer();

    for (uint32 y = 0; y < Display::SCREEN_HEIGHT; y++)
    {
        const byte *inLine = frameBuffer + (y * (Display::SCREEN_WIDTH * 4));
        byte *outLine = (byte *)m_surface->pixels + (y * (uint32)m_surface->pitch);

        for (uint32 x = 0; x < Display::SCREEN_WIDTH; x++)
        {
            outLine[0] = inLine[2];
            outLine[1] = inLine[1];
            outLine[2] = inLine[0];

            inLine += 4;
            outLine += 4;
        }
    }

    SDL_UpdateWindowSurface(m_window);
}

void System::UpdateTimer(uint32 clocks)
{
    //bool timer_control_changed = m_timer_control_changed;
    //m_timer_control_changed = false;

    // cpu runs at 4,194,304hz
    // timer runs at 16,384hz
    // therefore, every 256 cpu "clocks" equals one timer tick
    m_timer_divider_clocks += clocks;
    while (m_timer_divider_clocks >= 256)
    {
        m_timer_divider++;
        m_timer_divider_clocks -= 256;
    }

    // timer start/stop
    if (!(m_timer_control & 0x4))
        return;

    // 

    // add cycles
    m_timer_clocks += clocks;

    // find timer rate
    //static const uint32 clock_rates[] = { 4096, 262144, 65536, 16384 };
    static const uint32 clocks_per_timer_ticks[] = { 1024, 16, 64, 256 };
    uint32 clocks_per_timer_tick = clocks_per_timer_ticks[m_timer_control & 0x3];

    // cap at one iteration when the rate changes
    //if (m_timer_control_changed && m_timer_cycles > clock_rate)
        //m_timer_cycles %= clock_rate;

    // increment timer
    while (m_timer_clocks >= clocks_per_timer_tick)
    {
        if ((++m_timer_counter) == 0x00)
        {
            // timer overflow
            CPUInterruptRequest(CPU_INT_TIMER);
            m_timer_counter = m_timer_overflow_value;
        }

        m_timer_clocks -= clocks_per_timer_tick;
    }
}

void System::DisassembleCart(const char *outfile)
{
    ByteStream *pStream = FileSystem::OpenFile(outfile, BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_WRITE | BYTESTREAM_OPEN_TRUNCATE);
    if (pStream == nullptr)
        return;

    CPU::DisassembleFrom(this, 0x0000, 0x8000, pStream);
    pStream->Release();
}

uint8 System::CPURead(uint16 address) const
{
//     if (address == 0xc009)
//         __debugbreak();

    switch (address & 0xF000)
    {
        // cart memory
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
    case 0xA000:
    case 0xB000:
        {
            if (m_biosLatch)
                return (m_bios != nullptr && address < GB_BIOS_LENGTH) ? m_bios[address] : 0x00;

            // Cart read
            return (m_cartridge != nullptr) ? m_cartridge->CPURead(address) : 0x00;
        }

        // video memory
    case 0x8000:
    case 0x9000:
        return m_memory_vram[address & 0x1FFF];

        // working ram
    case 0xC000:
    case 0xD000:
        return m_memory_wram[address & 0x1FFF];

        // working ram shadow
    case 0xE000:
        return m_memory_wram[address & 0x1FFF];

        // working ram shadow, i/o, zero-page
    case 0xF000:
        {
            switch (address & 0x0F00)
            {
                // working ram shadow
            case 0x000:
            case 0x100:
            case 0x200:
            case 0x300:
            case 0x400:
            case 0x500:
            case 0x600:
            case 0x700:
            case 0x800:
            case 0x900:
            case 0xA00:
            case 0xB00:
            case 0xC00:
            case 0xD00:
                return m_memory_wram[address & 0x1FFF];

                // oam
            case 0xE00:
                // cpu can read oam???
                return (address < 0xFEA0) ? m_memory_oam[address & 0xFF] : 0x00;

                // zero page
            case 0xF00:
                {
                    if (address >= 0xFF80 && address < 0xFFFF)
                    {
                        // fast ram
                        return m_memory_zram[address - 0xFF80];
                    }
                    else
                    {
                        // IO registers, slow access
                        return CPUReadIORegister(address & 0xFF);
                    }
                }
            }
        }
    }

    // unhandled read
    Log_DevPrintf("Unhandled CPU read address 0x%04X", address);
    return 0x00;
}

void System::CPUWrite(uint16 address, uint8 value)
{
//     if (address == 0xc009)
//         __debugbreak();

    switch (address & 0xF000)
    {
        // cart memory
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
    case 0xA000:
    case 0xB000:
        {
            if (m_cartridge != nullptr)
                m_cartridge->CPUWrite(address, value);

            return;
        }

        // vram
    case 0x8000:
    case 0x9000:
        m_memory_vram[address & 0x1FFF] = value;
        return;

        // working ram
    case 0xC000:
    case 0xD000:
        m_memory_wram[address & 0x1FFF] = value;
        return;

        // working ram shadow
    case 0xE000:
        m_memory_wram[address & 0x1FFF] = value;
        return;

        // working ram shadow, i/o, zero-page
    case 0xF000:
        {
            switch (address & 0x0F00)
            {
                // working ram shadow
            case 0x000:
            case 0x100:
            case 0x200:
            case 0x300:
            case 0x400:
            case 0x500:
            case 0x600:
            case 0x700:
            case 0x800:
            case 0x900:
            case 0xA00:
            case 0xB00:
            case 0xC00:
            case 0xD00:
                m_memory_wram[address & 0x1FFF] = value;
                return;

                // oam
            case 0xE00:
                {
                    if (address < 0xFEA0)
                        m_memory_oam[address & 0xFF] = value;

                    return;
                }

                // zero page
            case 0xF00:
                {
                    if (address >= 0xFF80 && address < 0xFFFF)
                    {
                        // fast ram
                        m_memory_zram[address - 0xFF80] = value;
                        return;
                    }
                    else
                    {
                        // IO registers, slow access
                        CPUWriteIORegister(address & 0xFF, value);
                        return;
                    }
                }
            }
        }
    }

    // unhandled write
    Log_DevPrintf("Unhandled CPU write address 0x%04X (value 0x%02X)", address, value);
}

uint8 System::CPUReadIORegister(uint8 index) const
{
    switch (index & 0xF0)
    {
    case 0x00:
        {
            switch (index & 0x0F)
            {
                // Joypad
            case 0x00:
                {
                    if ((m_pad_row_select & 0x10) == 0)
                        return m_pad_row_select | m_pad_direction_state;
                    else if ((m_pad_row_select & 0x20) == 0)
                        return m_pad_row_select | m_pad_button_state;

                    return m_pad_row_select | 0x0F;
                }

                // Divider timer
            case 0x04:
                return m_timer_divider;

                // Counter timer
            case 0x05:
                return m_timer_counter;
            case 0x06:
                return m_timer_overflow_value;
            case 0x07:
                return m_timer_control;

                // Interrupt flag
            case 0x0F:
                return m_cpu->GetRegisters()->IF;
            }

            break;
        }

    case 0x40:
        {
            // LCD registers
            if (index <= 0x4B)
                return m_display->GetRegister((DISPLAY_REG)(index - 0x40));

            break;
        }

    case 0xF0:
        {
            switch (index & 0x0F)
            {
                // FFFF = IF
            case 0x0F:
                return m_cpu->GetRegisters()->IF;

            }
            break;
        }
    }

    // "high ram"
    if (index >= 0x80)
    {
        DebugAssert(index != 0xFF);
        return m_memory_zram[index - 0x80];
    }

    Log_DevPrintf("Unhandled CPU IO register read: 0x%02X", index);
    return 0x00;
}

void System::CPUWriteIORegister(uint8 index, uint8 value)
{
    switch (index & 0xF0)
    {
    case 0x00:
        {
            switch (index & 0x0F)
            {
                // pad select
            case 0x00:
                m_pad_row_select = value & 0x30;
                return;

                // FF01 - SB serial data
            case 0x01:
                Log_DevPrintf("Serial data written: 0x%02X (%c)", value, value);
                return;

                // FF02 - SC serial control
            case 0x02:
                return;                

                // Divider timer
            case 0x04:
                m_timer_divider = 0;
                m_timer_divider_clocks = 0; // <-- correct?
                return;

                // Counter timer
            case 0x05:
                m_timer_counter = 0;
                m_timer_clocks = 0;
                return;
            case 0x06:
                m_timer_overflow_value = value;
                return;
            case 0x07:
                m_timer_control = value;
                return;

                // interrupt flag
            case 0x0F:
                m_cpu->GetRegisters()->IF = value;
                return;
            }

            break;
        }
    case 0x10:
        {
            switch (index & 0x0F)
            {
                // FF10 - NR10 - Channel 1 Sweep register (R/W)
            case 0x00:
                return;

                // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
            case 0x02:
                return;

                // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
            case 0x03:
                return;

                // FF14 - NR14 - Channel 1 Frequency hi (R/W)
            case 0x04:
                return;

                // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
            case 0x09:
                return;

                // FF1A - NR30 - Channel 3 Sound on/off (R/W)
            case 0x0A:
                return;

                // FF1B - NR31 - Channel 3 Sound Length
            case 0x0B:
                return;

                // FF1C - NR32 - Channel 3 Select output level (R/W)
            case 0x0C:
                return;

                // FF1D - NR33 - Channel 3 Frequency's lower data (W)
            case 0x0D:
                return;

                // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
            case 0x0E:
                return;
            }

            break;
        }
    case 0x20:
        {
            switch (index & 0x0F)
            {
                // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
            case 0x04:
                return;

                // FF25 - NR51 - Selection of Sound output terminal (R/W)
            case 0x05:
                return;

                // FF26 - NR52 - sound on/off
            case 0x06:
                return;
            }

            break;
        }
    case 0x40:
        {
            switch (index & 0x0F)
            {
            case 0x00:      // FF40 - LCDC - LCD Control (R/W)
            case 0x01:      // FF41 - STAT - LCDC Status (R/W)
            case 0x02:      // FF42 - SCY - Scroll Y (R/W)
            case 0x03:      // FF43 - SCX - Scroll X (R/W)
            case 0x04:      // FF44 - LY - LCDC Y-Coordinate (R)
            case 0x05:      // FF45 - LYC - LY Compare(R / W)
            case 0x07:      // FF47 - BGP - BG Palette Data (R/W) - Non CGB Mode Only
            case 0x08:      // FF48 - OBP0 - Object Palette 0 Data (R/W) - Non CGB Mode Only
            case 0x09:      // FF49 - OBP1 - Object Palette 1 Data(R / W) - Non CGB Mode Only
            case 0x0A:      // FF4A - WY - Window Y Position (R/W)
            case 0x0B:      // FF4B - WX - Window X Position minus 7 (R/W)
                m_display->SetRegister((DISPLAY_REG)(index - 0x40), value);
                return;

                // FF46 - DMA - DMA Transfer and Start Address (W)
            case 0x06:
                {
                    // Writing to this register launches a DMA transfer from ROM or RAM to OAM memory (sprite attribute table). The written value specifies the transfer source address divided by 100h
                    // It takes 160 microseconds until the transfer has completed (80 microseconds in CGB Double Speed Mode), during this time the CPU can access only HRAM (memory at FF80-FFFE).
                    uint16 source_address = (uint16)value * 256;
                    uint16 destination_address = 0xFE00;
                    //Log_DevPrintf("OAM DMA Transfer 0x%04X -> 0x%04X", source_address, destination_address);
                    DMATransfer(source_address, destination_address, 160);
                    return;
                }

                // FF4F - VBK - CGB Mode Only - VRAM Bank
            case 0x0F:
                return;
            }

            break;
        }
    case 0x50:
        {
            switch (index & 0x0F)
            {
                // BIOS enable/disable latch
            case 0x00:
                m_biosLatch = (value == 0);
                return;
            }

            break;
        }
    case 0x60:
        {
            switch (index & 0x0F)
            {
                // FF68 - BCPS / BGPI - CGB Mode Only - Background Palette Index
            case 0x08:
                return;

                // FF69 - BCPD/BGPD - CGB Mode Only - Background Palette Data
            case 0x09:
                return;

                // FF6A - OCPS/OBPI - CGB Mode Only - Sprite Palette Index
            case 0x0A:
                return;

                // FF6B - OCPD/OBPD - CGB Mode Only - Sprite Palette Data
            case 0x0B:
                return;
            }

            break;

        }
    case 0xF0:
        {
            switch (index & 0x0F)
            {
            case 0x0F:
                // F0-FE is high ram below, FF = interrupt flag
                m_cpu->GetRegisters()->IE = value;
                return;
            }

            break;
        }
    }

    // "high ram"
    if (index >= 0x80)
    {
        DebugAssert(index != 0xFF);
        m_memory_zram[index - 0x80] = value;
        return;
    }

    Log_DevPrintf("Unhandled CPU IO register write: 0x%02X (value 0x%02X)", index, value);
}

void System::CPUInterruptRequest(uint8 index)
{
    m_cpu->RaiseInterrupt(index);
}
