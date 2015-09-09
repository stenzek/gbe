#include "system.h"
#include "cartridge.h"
#include "cpu.h"
#include "display.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Log.h"
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

void System::Reset()
{
    m_cpu->Reset();
    m_display->Reset();
    ResetMemory();

    // if bios not provided, emulate post-bootstrap state
    if (m_bios == nullptr)
    {
        m_biosLatch = false;
        SetPostBootstrapState();
    }

    CopyFrameBufferToSurface();
}

void System::Step()
{
    uint32 cycles = m_cpu->Step();
    for (uint32 i = 0; i < cycles; i++)
    {
        // TODO: Pass cycles as argument
        if (m_display->Step())
            CopyFrameBufferToSurface();
    }
}

void System::ResetMemory()
{
    // bios is initially mapped
    m_biosLatch = true;

    // zero all memory
    Y_memzero(m_memory_vram, sizeof(m_memory_vram));
    Y_memzero(m_memory_eram, sizeof(m_memory_eram));
    Y_memzero(m_memory_wram, sizeof(m_memory_wram));
    Y_memzero(m_memory_oam, sizeof(m_memory_oam));
    Y_memzero(m_memory_zram, sizeof(m_memory_zram));
}

void System::SetPostBootstrapState()
{

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

uint8 System::CPURead(uint16 address) const
{
    switch (address & 0xF000)
    {
        // bios/rom0
    case 0x0000:
        {
            if (m_biosLatch && address < GB_BIOS_LENGTH)
                return m_bios[address];

            return (m_cartridge != nullptr) ? m_cartridge->GetROM0()[address] : 0x00;
        }

        // rom0
    case 0x1000:
    case 0x2000:
    case 0x3000:
        return (m_cartridge != nullptr) ? m_cartridge->GetROM0()[address] : 0x00;

        // rom1
    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
        return (m_cartridge != nullptr && m_cartridge->HasROM1()) ? m_cartridge->GetROM1()[address & 0x3FFF] : 0x00;

        // vram
    case 0x8000:
    case 0x9000:
        return m_memory_vram[address & 0x1FFF];

        // eram
    case 0xA000:
    case 0xB000:
        return m_memory_eram[address & 0x1FFF];

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
                    if (address >= 0xFF80)
                    {
                        // fast ram
                        return m_memory_zram[address & 0x7F];
                    }
                    else
                    {
                        // IO registers, slow access
                        return CPUReadIORegister(address & 0x7F);
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
    switch (address & 0xF000)
    {
        // ROM0/ROM1 not writable
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
        return;

        // vram
    case 0x8000:
    case 0x9000:
        m_memory_vram[address & 0x1FFF] = value;
        return;

        // eram
    case 0xA000:
    case 0xB000:
        m_memory_eram[address & 0x1FFF] = value;
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
                    if (address >= 0xFF80)
                    {
                        // fast ram
                        m_memory_zram[address & 0x7F] = value;
                        return;
                    }
                    else
                    {
                        // IO registers, slow access
                        CPUWriteIORegister(address & 0x7F, value);
                        return;
                    }
                }
            }
        }
        break;
    }

    // unhandled write
    Log_DevPrintf("Unhandled CPU write address 0x%04X (value 0x%02X)", address, value);
}

uint8 System::CPUReadIORegister(uint8 index) const
{
    switch (index & 0xF0)
    {
    case 0x40:
        {
            // LCD registers
            if (index <= 0x4B)
                return m_display->GetRegister((DISPLAY_REG)(index - 0x40));
        }
    }

    Log_DevPrintf("Unhandled CPU IO register read: 0x%02X", index);
    return 0x00;
}

void System::CPUWriteIORegister(uint8 index, uint8 value)
{
    switch (index & 0xF0)
    {
    case 0x40:
        {
            // LCD registers
            if (index <= 0x4B)
            {
                m_display->SetRegister((DISPLAY_REG)(index - 0x40), value);
                return;
            }
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
        }
    }

    Log_DevPrintf("Unhandled CPU IO register write: 0x%02X (value 0x%02X)", index, value);
}

void System::CPUInterruptRequest(uint8 index)
{
    DebugAssert(index <= 4);
    m_cpu->GetRegisters()->IF |= (1 << index);
}
