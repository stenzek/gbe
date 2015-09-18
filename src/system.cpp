#include "system.h"
#include "cartridge.h"
#include "cpu.h"
#include "display.h"
#include "audio.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include "YBaseLib/Math.h"
#include <cmath>
Log_SetChannel(System);

System::System()
{
    m_cpu = nullptr;
    m_display = nullptr;
    m_audio = nullptr;
    m_cartridge = nullptr;
    m_callbacks = nullptr;
    m_bios = nullptr;
    m_biosLatch = false;
    m_vramLocked = false;
    m_oamLocked = false;
}

System::~System()
{
    delete m_audio;
    delete m_display;
    delete m_cpu;
}

bool System::Init(CallbackInterface *callbacks, SYSTEM_MODE mode, const byte *bios, Cartridge *cartridge)
{
    m_mode = (mode == NUM_SYSTEM_MODES) ? cartridge->GetSystemMode() : mode;
    m_callbacks = callbacks;
    m_bios = bios;
    m_cartridge = cartridge;

    m_cpu = new CPU(this);
    m_display = new Display(this);
    m_audio = new Audio(this);

    m_clocks_since_reset = 0;
    m_speed_multiplier = 1.0f;

    m_frame_limiter = true;
    m_frame_counter = 0;
    m_accurate_timing = true;

    m_memory_locked_cycles = 0;
    m_memory_permissive = false;

    m_high_wram_bank = 1;
    m_vram_bank = 0;
    m_cgb_speed_switch = 0;

    Log_InfoPrintf("Initialized system in mode %s.", NameTable_GetNameString(NameTables::SystemMode, m_mode));
    return true;
}

void System::Reset()
{
    m_cpu->Reset();
    m_display->Reset();
    m_audio->Reset();
    ResetMemory();
    ResetTimer();
    ResetPad();
    if (m_cartridge != nullptr)
        m_cartridge->Reset();

    // if bios not provided, emulate post-bootstrap state
    if (m_bios == nullptr || m_mode != SYSTEM_MODE_DMG)
        SetPostBootstrapState();

    m_clocks_since_reset = 0;
    m_reset_timer.Reset();
    m_frame_counter = 0;

    m_memory_locked_cycles = 0;

    m_high_wram_bank = 1;
    m_vram_bank = 0;
    m_cgb_speed_switch = 0;
}

void System::Step()
{
    uint32 clocks = m_cpu->Step();
    DebugAssert((clocks % 4) == 0);

    // Make each instruction take half as long in double-speed mode.
    uint32 slow_speed_shift = (m_cgb_speed_switch >> 7);
    uint32 slow_speed_clocks = clocks >> slow_speed_shift;

    // Handle memory locking for OAM transfers [affected by double speed]
    if (m_memory_locked_cycles > 0)
        m_memory_locked_cycles = (clocks > m_memory_locked_cycles) ? 0 : (clocks - slow_speed_clocks);

    // Simulate display [not affected by double speed]
    m_display->ExecuteFor(slow_speed_clocks);

    // Simulate timers [affected by double speed]
    UpdateTimer(clocks);

    // update our counter [use the normal speed as a reference]
    m_clocks_since_reset += slow_speed_clocks;
}

uint64 System::TimeToClocks(double time)
{
    // cpu runs at 4,194,304hz
    return (uint64)(time * 4194304.0);
}

double System::ExecuteFrame()
{
    static const float VBLANK_INTERVAL = 0.0166f;   //16.6ms

    // framelimiter on?
    if (m_frame_limiter)
    {
        // using "accurate" timing?
        if (m_accurate_timing)
        {
            // determine the number of cycles we should be at
            double frame_start_time = m_reset_timer.GetTimeSeconds();
            uint64 target_clocks = TimeToClocks(frame_start_time);
            uint64 current_clocks = m_clocks_since_reset;
            TRACE("target_clocks = %u, current_clocks = %u", (uint32)target_clocks, (uint32)current_clocks);
            m_current_speed = 1.0f;

            // check that we're not ahead (is perfectly possible since each instruction takes a minimum of 4 clocks)
            if (target_clocks > current_clocks)
            {
                // keep executing until we meet our target
                while (m_clocks_since_reset < target_clocks)
                    Step();
            }

            // calculate the ideal time we want to hit the next frame
            double frame_end_time = m_reset_timer.GetTimeSeconds();
            double execution_time = frame_end_time - frame_start_time;

            // vblank is every 16.6ms, so the time we want is the next multiple of this
            double next_vblank_time = frame_start_time + (VBLANK_INTERVAL - std::fmod(frame_start_time, VBLANK_INTERVAL));
            double sleep_time = Max(next_vblank_time - frame_end_time - execution_time, 0.0);
            TRACE("frame_start_time = %f, execution_time = %f, next_vblank_time = %f, sleep time: %f", frame_start_time, execution_time, next_vblank_time, sleep_time);
            return sleep_time;
        }
        else
        {
            // get difference in time
            double time_diff = m_reset_timer.GetTimeSeconds();
            m_reset_timer.Reset();
            m_clocks_since_reset = 0;

            // get the number of cycles to execute
            uint64 target_clocks = TimeToClocks(time_diff * m_speed_multiplier);

            // attempt to execute this many cycles, bail out if we exceed vblank time
            while (m_clocks_since_reset < target_clocks && m_reset_timer.GetTimeSeconds() < VBLANK_INTERVAL)
                Step();

            // calculate the speed we're at
            m_current_speed = float((double)m_clocks_since_reset / (double)target_clocks) * m_speed_multiplier;

            // calculate the sleep time
            double sleep_time = Max((VBLANK_INTERVAL / m_speed_multiplier) - m_reset_timer.GetTimeSeconds(), 0.0);
            return sleep_time;
        }
    }
    else
    {
        // framelimiter off, just execute as many as quickly as possible, say, 16ms worth at a time
        uint64 target_clocks = m_clocks_since_reset + TimeToClocks((double)VBLANK_INTERVAL);
        while (m_clocks_since_reset < target_clocks)
            Step();

        // update speed every 100ms
        if (m_reset_timer.GetTimeSeconds() > 0.1f)
        {
            uint64 fullspeed_clocks = TimeToClocks(m_reset_timer.GetTimeSeconds());
            m_current_speed = float(double(m_clocks_since_reset) / double(fullspeed_clocks));
            m_clocks_since_reset = 0;
            m_reset_timer.Reset();
        }

        // don't sleep
        return 0.0;
    }
}

void System::SetPadDirection(PAD_DIRECTION direction)
{
    uint8 old_direction_state = m_pad_direction_state;
    m_pad_direction_state = ~(PAD_DIRECTION_MASK & direction) & PAD_DIRECTION_MASK;
    if (old_direction_state != m_pad_direction_state)
    {
        TRACE("Pad direction set to 0x%02X", direction);
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
        TRACE("Pad direction 0x%02X set %s", direction, state ? "on" : "off");
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
        TRACE("Pad button 0x%02X set %s", button, state ? "on" : "off");
        CPUInterruptRequest(CPU_INT_JOYPAD);
    }
}

void System::SetTargetSpeed(float multiplier)
{
    m_speed_multiplier = multiplier;
    m_reset_timer.Reset();
    m_clocks_since_reset = 0;
}

void System::SetAccurateTiming(bool on)
{
    m_accurate_timing = on;
    m_reset_timer.Reset();
    m_clocks_since_reset = 0;
}

void System::DMATransfer(uint16 source_address, uint16 destination_address, uint32 bytes)
{
    // slow but due to the ranges has to be done this awy
    uint16 current_source_address = source_address;
    uint16 current_destination_address = destination_address;
    for (uint32 i = 0; i < bytes; i++, current_source_address++, current_destination_address++)
        CPUWrite(current_destination_address, CPURead(current_source_address));

    // Stall memory access for 160 microseconds
    // This number here should be 671, but because part of the time of an instruction
    // can be consumed by another memory read, and we're not handling that yet, we'll
    // give it a few extra cycles.
    //m_memory_locked_cycles = 671;
    m_memory_locked_cycles = 640;
}

void System::SwitchCGBSpeed()
{
    if (!(m_cgb_speed_switch & (1 << 0)))
        return;

    // Flips the switch bit off at the same time.
    m_cgb_speed_switch ^= 0x81;
    if (m_cgb_speed_switch & 0x80)
        Log_DevPrintf("Switching to CGB double speed mode.");
    else
        Log_DevPrintf("Switching to normal speed mode.");

    // reset timing - so that accurate timing doesn't break
    //m_clocks_since_reset = 0;
    //m_reset_timer.Reset();
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
    registers->AF = (InCGBMode()) ? 0x11B0 : 0x01B0;
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
    CPUWriteIORegister(0x26, (InSGBMode()) ? 0xF0 : 0xF1);     // NR52 (F0 on SGB)
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

    // when DMA transfer is in progress, all memory except FF80-FFFE is inaccessible
    if (m_memory_locked_cycles > 0 && !m_memory_permissive && (address < 0xFF80 || address > 0xFFFE))
    {
        Log_WarningPrintf("CPU read of address 0x%04X denied during DMA transfer", address);
        return 0x00;
    }

    // select address range
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
            if (m_biosLatch && address < GB_BIOS_LENGTH)
                return (m_bios != nullptr) ? m_bios[address] : 0x00;

            // Cart read
            return (m_cartridge != nullptr) ? m_cartridge->CPURead(address) : 0x00;
        }

        // video memory
    case 0x8000:
    case 0x9000:
        {
            if (m_vramLocked && !m_memory_permissive)
            {
                // Apparently returns 0xFF?
                Log_WarningPrintf("CPU read of VRAM address 0x%04X while locked.", address);
                return 0xFF;
            }

            return m_memory_vram[m_vram_bank][address & 0x1FFF];
        }

        // working ram
    case 0xC000:
        return m_memory_wram[0][address & 0xFFF];

    case 0xD000:
        return m_memory_wram[m_high_wram_bank][address & 0xFFF];

        // working ram shadow
    case 0xE000:
        return m_memory_wram[0][address & 0xFFF];

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
                return m_memory_wram[m_high_wram_bank][address & 0xFFF];

                // oam
            case 0xE00:
                {
                    if (m_oamLocked && !m_memory_permissive)
                    {
                        // Apparently returns 0xFF?
                        Log_WarningPrintf("CPU read of OAM address 0x%04X while locked.", address);
                        return 0xFF;
                    }
                    else if (address >= 0xFEA0)
                    {
                        Log_WarningPrintf("Out-of-range read of OAM address 0x%04X", address);
                        return 0x00;
                    }

                    return m_memory_oam[address & 0xFF];
                }

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
    Log_WarningPrintf("Unhandled CPU read address 0x%04X", address);
    return 0x00;
}

void System::CPUWrite(uint16 address, uint8 value)
{
//     if (address == 0xc009)
//         __debugbreak();

    // when DMA transfer is in progress, all memory except FF80-FFFE is inaccessible
    if (m_memory_locked_cycles > 0 && !m_memory_permissive && (address < 0xFF80 || address > 0xFFFE))
    {
        Log_WarningPrintf("CPU write of address 0x%04X (value 0x%02X) denied during DMA transfer", address, value);
        return;
    }

    // select memory range
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
        {
            if (m_vramLocked && !m_memory_permissive)
            {
                Log_WarningPrintf("CPU write of VRAM address 0x%04X (value 0x%02X) while locked.", address, value);
                return;
            }

            m_memory_vram[m_vram_bank][address & 0x1FFF] = value;
            return;
        }


        // working ram
    case 0xC000:
        m_memory_wram[0][address & 0xFFF] = value;
        return;

    case 0xD000:
        m_memory_wram[m_high_wram_bank][address & 0xFFF] = value;
        return;

        // working ram shadow
    case 0xE000:
        m_memory_wram[0][address & 0xFFF] = value;
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
                m_memory_wram[m_high_wram_bank][address & 0xFFF] = value;
                return;

                // oam
            case 0xE00:
                {
                    if (m_oamLocked && !m_memory_permissive)
                    {
                        // Apparently returns 0xFF?
                        Log_WarningPrintf("CPU write of OAM address 0x%04X (value 0x%02X) while locked.", address, value);
                        return;
                    }
                    else if (address >= 0xFEA0)
                    {
                        Log_WarningPrintf("Out-of-range write of OAM address 0x04X (value 0x%02X)", address, value);
                        return;
                    }

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
    Log_WarningPrintf("Unhandled CPU write address 0x%04X (value 0x%02X)", address, value);
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

                // FF04 - DIV - Divider Register (R/W)
            case 0x04:
                return m_timer_divider;

                // FF05 - TIMA - Timer counter (R/W)
            case 0x05:
                return m_timer_counter;

                // FF06 - TMA - Timer Modulo (R/W)
            case 0x06:
                return m_timer_overflow_value;

                // FF07 - TAC - Timer Control (R/W)
            case 0x07:
                return m_timer_control;

                // FF0F - IF - Interrupt Flag (R/W)
            case 0x0F:
                return m_cpu->GetRegisters()->IF;
            }

            break;
        }

    case 0x10:
        {
            switch (index & 0x0F)
            {
            case 0x00:      // FF10 - NR10 - Channel 1 Sweep register (R/W)
            case 0x01:      // FF11 - NR11 - Channel 1 Sound length/Wave pattern duty (R/W)
            case 0x02:      // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
            case 0x03:      // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
            case 0x04:      // FF14 - NR14 - Channel 1 Frequency hi (R/W)
            case 0x06:      // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
            case 0x07:      // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
            case 0x08:      // FF18 - NR23 - Channel 2 Frequency lo data (W)
            case 0x09:      // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
            case 0x0A:      // FF1A - NR30 - Channel 3 Sound on/off (R/W)
            case 0x0B:      // FF1B - NR31 - Channel 3 Sound Length
            case 0x0C:      // FF1C - NR32 - Channel 3 Select output level (R/W)
            case 0x0D:      // FF1D - NR33 - Channel 3 Frequency's lower data (W)
            case 0x0E:      // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
                return m_audio->CPUReadRegister(index);
            }

            break;
        }

    case 0x20:
        {
            switch (index & 0x0F)
            {
            case 0x00:      // FF20 - NR41 - Channel 4 Sound Length (R/W)
            case 0x01:      // FF21 - NR42 - Channel 4 Volume Envelope (R/W)
            case 0x02:      // FF22 - NR43 - Channel 4 Polynomial Counter (R/W)
            case 0x03:      // FF23 - NR44 - Channel 4 Counter/consecutive; Inital (R/W)
            case 0x04:      // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
            case 0x05:      // FF25 - NR51 - Selection of Sound output terminal (R/W)
            case 0x06:      // FF26 - NR52 - sound on/off
                return m_audio->CPUReadRegister(index);
            }

            break;
        }

    case 0x30:
        {
            // FF30-FF3F - Wave Pattern RAM
            return m_audio->CPUReadRegister(index);
        }

    case 0x40:
        {
            // LCD registers
            switch (index & 0x0F)
            {
            case 0x00:      // FF40 - LCDC - LCD Control (R/W)
            case 0x01:      // FF41 - STAT - LCDC Status (R/W)
            case 0x02:      // FF42 - SCY - Scroll Y (R/W)
            case 0x03:      // FF43 - SCX - Scroll X (R/W)
            case 0x04:      // FF44 - LY - LCDC Y-Coordinate (R/W?)
            case 0x05:      // FF45 - LYC - LY Compare(R / W)
            case 0x07:      // FF47 - BGP - BG Palette Data (R/W) - Non CGB Mode Only
            case 0x08:      // FF48 - OBP0 - Object Palette 0 Data (R/W) - Non CGB Mode Only
            case 0x09:      // FF49 - OBP1 - Object Palette 1 Data(R / W) - Non CGB Mode Only
            case 0x0A:      // FF4A - WY - Window Y Position (R/W)
            case 0x0B:      // FF4B - WX - Window X Position minus 7 (R/W)
                return m_display->CPUReadRegister(index);
            }

            break;
        }

    case 0x50:
        {
            // LCD registers
            switch (index & 0x0F)
            {
            case 0x00:      // FF50 - BIOS latch
                return m_biosLatch;
            }

            break;
        }



    case 0xF0:
        {
            switch (index & 0x0F)
            {
                // FFFF = IE
            case 0x0F:
                return m_cpu->GetRegisters()->IE;

            }
            break;
        }
    }

    // CGB-only registers
    if (InCGBMode())
    {
        switch (index & 0xF0)
        {
        case 0x40:
            {
                switch (index & 0x0F)
                {
                case 0x0F:      // FF4F - VBK - CGB Mode Only - VRAM Bank
                    return m_vram_bank;

                case 0x0D:      // FF4D - KEY1 - CGB Mode Only - Prepare Speed Switch
                    return m_cgb_speed_switch;
                }

                break;
            }

        case 0x50:
            {
                switch (index & 0x0F)
                {
                case 0x01:      // FF51 - HDMA1 - CGB Mode Only - New DMA Source, High
                case 0x02:      // FF52 - HDMA2 - CGB Mode Only - New DMA Source, Low
                case 0x03:      // FF53 - HDMA3 - CGB Mode Only - New DMA Destination, High
                case 0x04:      // FF54 - HDMA4 - CGB Mode Only - New DMA Destination, Low
                case 0x05:      // FF55 - HDMA5 - CGB Mode Only - New DMA Length/Mode/Start
                    return m_display->CPUReadRegister(index);
                }

                break;
            }

        case 0x60:
            {
                // LCD registers
                switch (index & 0x0F)
                {
                case 0x08:      // FF68 - BCPS/BGPI - CGB Mode Only - Background Palette Index
                case 0x09:      // FF69 - BCPD/BGPD - CGB Mode Only - Background Palette Data
                case 0x0A:      // FF6A - OCPS/OBPI - CGB Mode Only - Sprite Palette Index
                case 0x0B:      // FF6B - OCPD/OBPD - CGB Mode Only - Sprite Palette Data
                    return m_display->CPUReadRegister(index);
                }

                break;
            }

        case 0x70:
            {
                switch (index & 0x0F)
                {
                case 0x70:      // FF70 - SVBK - CGB Mode Only - WRAM Bank
                    return (InCGBMode()) ? m_high_wram_bank : 0x00;
                }

                break;
            }
        }
    }

    // "high ram"
    if (index >= 0x80)
    {
        DebugAssert(index != 0xFF);
        return m_memory_zram[index - 0x80];
    }

    Log_WarningPrintf("Unhandled CPU IO register read: 0x%02X", index);
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

                // FF04 - DIV - Divider Register (R/W)
            case 0x04:
                m_timer_divider = 0;
                return;

                // FF05 - TIMA - Timer counter (R/W)
            case 0x05:
                m_timer_counter = value;
                return;

                // FF06 - TMA - Timer Modulo (R/W)
            case 0x06:
                m_timer_overflow_value = value;
                return;

                // FF07 - TAC - Timer Control (R/W)
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
            case 0x00:      // FF10 - NR10 - Channel 1 Sweep register (R/W)
            case 0x01:      // FF11 - NR11 - Channel 1 Sound length/Wave pattern duty (R/W)
            case 0x02:      // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
            case 0x03:      // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
            case 0x04:      // FF14 - NR14 - Channel 1 Frequency hi (R/W)
            case 0x06:      // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
            case 0x07:      // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
            case 0x08:      // FF18 - NR23 - Channel 2 Frequency lo data (W)
            case 0x09:      // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
            case 0x0A:      // FF1A - NR30 - Channel 3 Sound on/off (R/W)
            case 0x0B:      // FF1B - NR31 - Channel 3 Sound Length
            case 0x0C:      // FF1C - NR32 - Channel 3 Select output level (R/W)
            case 0x0D:      // FF1D - NR33 - Channel 3 Frequency's lower data (W)
            case 0x0E:      // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
                m_audio->CPUWriteRegister(index, value);
                return;
            }

            break;
        }

    case 0x20:
        {
            switch (index & 0x0F)
            {
            case 0x00:      // FF20 - NR41 - Channel 4 Sound Length (R/W)
            case 0x01:      // FF21 - NR42 - Channel 4 Volume Envelope (R/W)
            case 0x02:      // FF22 - NR43 - Channel 4 Polynomial Counter (R/W)
            case 0x03:      // FF23 - NR44 - Channel 4 Counter/consecutive; Inital (R/W)
            case 0x04:      // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
            case 0x05:      // FF25 - NR51 - Selection of Sound output terminal (R/W)
            case 0x06:      // FF26 - NR52 - sound on/off
                m_audio->CPUWriteRegister(index, value);
                return;
            }

            break;
        }

    case 0x30:
        {
            // FF30-FF3F - Wave Pattern RAM
            m_audio->CPUWriteRegister(index, value);
            return;
        }

    case 0x40:
        {
            switch (index & 0x0F)
            {
            case 0x00:      // FF40 - LCDC - LCD Control (R/W)
            case 0x01:      // FF41 - STAT - LCDC Status (R/W)
            case 0x02:      // FF42 - SCY - Scroll Y (R/W)
            case 0x03:      // FF43 - SCX - Scroll X (R/W)
            case 0x04:      // FF44 - LY - LCDC Y-Coordinate (R/W?)
            case 0x05:      // FF45 - LYC - LY Compare(R / W)
            case 0x07:      // FF47 - BGP - BG Palette Data (R/W) - Non CGB Mode Only
            case 0x08:      // FF48 - OBP0 - Object Palette 0 Data (R/W) - Non CGB Mode Only
            case 0x09:      // FF49 - OBP1 - Object Palette 1 Data(R / W) - Non CGB Mode Only
            case 0x0A:      // FF4A - WY - Window Y Position (R/W)
            case 0x0B:      // FF4B - WX - Window X Position minus 7 (R/W)
                m_display->CPUWriteRegister(index, value);
                return;

            case 0x06:      // FF46 - DMA - DMA Transfer and Start Address (W)
                {
                    // Writing to this register launches a DMA transfer from ROM or RAM to OAM memory (sprite attribute table). The written value specifies the transfer source address divided by 100h
                    // It takes 160 microseconds until the transfer has completed (80 microseconds in CGB Double Speed Mode), during this time the CPU can access only HRAM (memory at FF80-FFFE).
                    uint16 source_address = (uint16)value * 256;
                    uint16 destination_address = 0xFE00;
                    //Log_DevPrintf("OAM DMA Transfer 0x%04X -> 0x%04X", source_address, destination_address);
                    DMATransfer(source_address, destination_address, 160);
                    return;
                }
            }

            break;
        }

    case 0x50:
        {
            switch (index & 0x0F)
            {
            case 0x00:      // FF00 - BIOS enable/disable latch
                m_biosLatch = (value == 0);
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

    // CGB-only registers
    if (InCGBMode())
    {
        switch (index & 0xF0)
        {
        case 0x40:
            {
                switch (index & 0x0F)
                {
                case 0x0F:      // FF4F - VBK - CGB Mode Only - VRAM Bank
                    m_vram_bank = value & 0x1;
                    return;

                case 0x0D:      // FF4D - KEY1 - CGB Mode Only - Prepare Speed Switch
                    m_cgb_speed_switch |= (value & 0x01);
                    return;
                }

                break;
            }

        case 0x50:
            {
                switch (index & 0x0F)
                {
                case 0x01:      // FF51 - HDMA1 - CGB Mode Only - New DMA Source, High
                case 0x02:      // FF52 - HDMA2 - CGB Mode Only - New DMA Source, Low
                case 0x03:      // FF53 - HDMA3 - CGB Mode Only - New DMA Destination, High
                case 0x04:      // FF54 - HDMA4 - CGB Mode Only - New DMA Destination, Low
                case 0x05:      // FF55 - HDMA5 - CGB Mode Only - New DMA Length/Mode/Start
                    m_display->CPUWriteRegister(index, value);
                    return;
                }

                break;
            }

        case 0x60:
            {
                switch (index & 0x0F)
                {
                case 0x08:      // FF68 - BCPS/BGPI - CGB Mode Only - Background Palette Index
                case 0x09:      // FF69 - BCPD/BGPD - CGB Mode Only - Background Palette Data
                case 0x0A:      // FF6A - OCPS/OBPI - CGB Mode Only - Sprite Palette Index
                case 0x0B:      // FF6B - OCPD/OBPD - CGB Mode Only - Sprite Palette Data
                    m_display->CPUWriteRegister(index, value);
                    return;
                }

                break;
            }

        case 0x70:
            {
                switch (index & 0x0F)
                {
                case 0x00:      // FF70 - SVBK - CGB Mode Only - WRAM Bank
                    m_high_wram_bank = value & 0x03;
                    return;
                }

                break;
            }
        }
    }

    // "high ram"
    if (index >= 0x80)
    {
        DebugAssert(index != 0xFF);
        m_memory_zram[index - 0x80] = value;
        return;
    }

    Log_WarningPrintf("Unhandled CPU IO register write: 0x%02X (value 0x%02X)", index, value);
}

void System::CPUInterruptRequest(uint8 index)
{
    TRACE("CPU raise interrupt %u", index);
    m_cpu->RaiseInterrupt(index);
}
