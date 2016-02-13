#include "system.h"
#include "cartridge.h"
#include "cpu.h"
#include "display.h"
#include "audio.h"
#include "serial.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include "YBaseLib/Math.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryWriter.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/Error.h"
#include <cmath>
Log_SetChannel(System);

// TODO: Split to separate files
const uint32 DMG_BIOS_LENGTH = 256;
const uint32 CGB_BIOS_LENGTH = 2048;

System::System(CallbackInterface *callbacks)
{
    m_cpu = nullptr;
    m_display = nullptr;
    m_audio = nullptr;
    m_serial = nullptr;
    m_cartridge = nullptr;
    m_callbacks = callbacks;
    m_bios = nullptr;
    m_bios_length = 0;
    m_biosLatch = false;
    m_vramLocked = false;
    m_oamLocked = false;
}

System::~System()
{
    delete m_serial;
    delete m_audio;
    delete m_display;
    delete m_cpu;
}

bool System::Init(SYSTEM_MODE mode, const byte *bios, uint32 bios_length, Cartridge *cartridge)
{
    m_mode = (mode == NUM_SYSTEM_MODES) ? cartridge->GetSystemMode() : mode;
    m_bios = bios;
    m_bios_length = (bios != nullptr) ? bios_length : 0;
    m_cartridge = cartridge;
    if (m_bios_length != 0)
    {
        if ((m_mode == SYSTEM_MODE_DMG && m_bios_length != DMG_BIOS_LENGTH) ||
            (m_mode == SYSTEM_MODE_CGB && m_bios_length != CGB_BIOS_LENGTH))
        {
            Log_ErrorPrintf("Incorrect bootstrap rom length");
            return false;
        }
    }

    m_cpu = new CPU(this);
    m_display = new Display(this);
    m_audio = new Audio(this);
    m_serial = new Serial(this);

    m_cycle_number = 0;
    m_last_sync_cycle = 0;
    m_next_display_sync_cycle = 0;
    m_next_audio_sync_cycle = 0;
    m_next_serial_sync_cycle = 0;
    m_next_timer_sync_cycle = 0;
    m_next_event_cycle = 0;
    m_event = false;

    m_reset_timer.Reset();
    m_clocks_since_reset = 0;
    m_last_vblank_clocks = 0;

    m_speed_timer.Reset();
    m_cycles_since_speed_update = 0;
    m_frames_since_speed_update = 0;
    m_current_fps = 0;

    m_speed_multiplier = 1.0f;

    m_frame_limiter = true;
    m_frame_counter = 0;
    m_accurate_timing = true;
    m_paused = false;
    m_serial_pause = false;

    m_memory_locked_cycles = 0;
    m_memory_locked_start = 0;
    m_memory_locked_end = 0;
    m_memory_permissive = false;

    m_high_wram_bank = 1;
    m_vram_bank = 0;
    m_cgb_speed_switch = 0;

    // init modules
    m_cpu->Reset();
    m_display->Reset();
    m_audio->Reset();
    m_serial->Reset();

    // clear our memory
    ResetMemory();
    ResetTimer();
    ResetPad();
    if (m_cartridge != nullptr)
        m_cartridge->Reset();

    // if bios not provided, emulate post-bootstrap state
    if (m_bios == nullptr)
        SetPostBootstrapState();

    Log_InfoPrintf("Initialized system in mode %s.", NameTable_GetNameString(NameTables::SystemMode, m_mode));
    return true;
}

void System::Reset()
{
    m_cycle_number = 0;
    m_last_sync_cycle = 0;
    m_next_display_sync_cycle = 0;
    m_next_audio_sync_cycle = 0;
    m_next_serial_sync_cycle = 0;
    m_next_timer_sync_cycle = 0;
    m_next_event_cycle = 0;
    m_event = false;

    m_reset_timer.Reset();
    m_clocks_since_reset = 0;
    m_last_vblank_clocks = 0;

    m_speed_timer.Reset();
    m_cycles_since_speed_update = 0;
    m_frames_since_speed_update = 0;
    m_current_fps = 0;

    m_frame_counter = 0;

    m_memory_locked_cycles = 0;

    m_high_wram_bank = 1;
    m_vram_bank = 0;
    m_cgb_speed_switch = 0;

    m_cpu->Reset();
    m_display->Reset();
    m_audio->Reset();
    m_serial->Reset();
    ResetMemory();
    ResetTimer();
    ResetPad();
    if (m_cartridge != nullptr)
        m_cartridge->Reset();

    // if bios not provided, emulate post-bootstrap state
    if (m_bios == nullptr)
        SetPostBootstrapState();
}

void System::SetPaused(bool paused)
{
    if (m_paused == paused)
        return;

    m_paused = paused;
    if (!m_paused)
    {
        m_reset_timer.Reset();
        m_clocks_since_reset = 0;
        m_last_vblank_clocks = 0;

        m_speed_timer.Reset();
        m_cycles_since_speed_update = 0;
    }
}

void System::Step()
{
    // handle serial pause
    if (m_serial_pause)
    {
        m_serial->Synchronize();
        return;
    }

    m_cpu->ExecuteInstruction();
}

void System::UpdateNextEventCycle()
{
    if (m_event)
        return;

    uint32 first_event = Min(m_next_timer_sync_cycle, Min(m_next_serial_sync_cycle, Min(m_next_display_sync_cycle, m_next_audio_sync_cycle)));
    DebugAssert(first_event >= m_cycle_number);
    if (m_memory_locked_cycles > 0)
        first_event = Min(first_event, m_cycle_number + m_memory_locked_cycles);

    uint32 clocks_to_event = (first_event - m_cycle_number);
    m_next_event_cycle = (int32)clocks_to_event;
}

void System::AddCPUCycles(uint32 cpu_clocks)
{
    // CPU clocks are always dividable by 4
    DebugAssert((cpu_clocks % 4) == 0);
    m_cycle_number += cpu_clocks;
    m_clocks_since_reset += (cpu_clocks >> GetDoubleSpeedDivider());
    m_cycles_since_speed_update += (cpu_clocks >> GetDoubleSpeedDivider());
    m_next_event_cycle -= (int32)cpu_clocks;
    if (m_next_event_cycle > 0)
        return;

    // what we will synchronize
    bool sync_timers = (m_cycle_number >= m_next_timer_sync_cycle);
    bool sync_serial = (m_cycle_number >= m_next_serial_sync_cycle);
    bool sync_display = (m_cycle_number >= m_next_display_sync_cycle);
    bool sync_audio = (m_cycle_number >= m_next_audio_sync_cycle);
    uint32 cycles_since_sync = CalculateDoubleSpeedCycleCount(m_last_sync_cycle);
    m_last_sync_cycle = m_cycle_number;
    m_event = true;

    // Handle memory locking for OAM transfers [affected by double speed]
    if (m_memory_locked_cycles > 0)
        m_memory_locked_cycles = (cycles_since_sync > m_memory_locked_cycles) ? 0 : (m_memory_locked_cycles - cycles_since_sync);

    // Simulate display [not affected by double speed]
    if (sync_display)
        m_display->Synchronize();

    // Simulate audio [not affected by double speed]
    if (sync_audio)
        m_audio->Synchronize();

    // Simulate serial [affected by double speed]
    if (sync_serial)
        m_serial->Synchronize();

    // Simulate timers [affected by double speed]
    if (sync_timers)
        SynchronizeTimers();

    // Update time to next event
    m_event = false;
    UpdateNextEventCycle();
}

void System::SetSerialPause(bool enabled)
{
    if (m_serial_pause == enabled)
        return;

    m_serial_pause = enabled;
    if (!m_serial_pause)
    {
        m_clocks_since_reset = 0;
        m_last_vblank_clocks = 0;
        m_reset_timer.Reset();
    }
}

void System::TriggerOAMBug()
{
    if (m_mode == SYSTEM_MODE_DMG && m_display->CanTriggerOAMBug())
    {
        // TODO: Should actually be moving OAM memory around, not random data.
        static const byte junk[152] = {
            0xCF, 0x93, 0xA1, 0x77, 0x90, 0x6B, 0xEC, 0xF2, 0xA7, 0xF4, 0x3C, 0xEF, 0x95, 0x34, 0xBD, 0x2C,
            0x4F, 0x73, 0x75, 0x01, 0x61, 0x1C, 0x91, 0xFC, 0xE8, 0x0C, 0x03, 0x06, 0xB1, 0x2F, 0xB3, 0x4D,
            0xA4, 0x14, 0xB1, 0xEA, 0x29, 0xEC, 0x21, 0x59, 0x4D, 0xF4, 0x41, 0x10, 0xA1, 0xDF, 0x51, 0x79,
            0x1F, 0x5D, 0xD7, 0x1C, 0x1D, 0xC1, 0xEC, 0x0D, 0xD6, 0xA3, 0xA7, 0x23, 0x33, 0xFC, 0x29, 0x07,
            0xB5, 0xD9, 0x7F, 0x98, 0xE9, 0x5C, 0x5E, 0x8C, 0x66, 0x11, 0xEB, 0xE7, 0xAB, 0x5F, 0x0C, 0x92,
            0x3E, 0xA2, 0x92, 0x1F, 0x44, 0x42, 0xA5, 0x83, 0x57, 0x16, 0x0F, 0x4C, 0xD0, 0x60, 0xB9, 0x26,
            0xB6, 0x77, 0x2E, 0x16, 0x98, 0xAA, 0x20, 0x03, 0x81, 0x67, 0xA0, 0x0F, 0x7B, 0xBF, 0xD7, 0xD6,
            0x88, 0x65, 0xCF, 0x21, 0xE2, 0x44, 0xD0, 0x89, 0xD4, 0x4A, 0xC9, 0x03, 0x7D, 0x87, 0x87, 0x55,
            0xAE, 0xFB, 0xDC, 0x3B, 0x23, 0xC2, 0x2D, 0x78, 0x28, 0x24, 0xB1, 0xF5, 0xAC, 0xAC, 0xA5, 0x34,
            0x30, 0x41, 0x8B, 0x2E, 0xAF, 0x4B, 0xBB, 0x9F
        };

        Y_memcpy(m_memory_oam + 8, junk, sizeof(junk));
        //Log_WarningPrintf("OAM bug invoked");
    }
}

uint64 System::TimeToClocks(double time)
{
    // cpu runs at 4,194,304hz
    return (uint64)(time * (4194304.0 * m_speed_multiplier));
}

double System::ClocksToTime(uint64 clocks)
{
    return (double)clocks / (4194304.0 * m_speed_multiplier);
}

double System::ExecuteFrame()
{
    static const float VBLANK_INTERVAL = 0.0166f;   //16.6ms

    if (m_paused)
        return VBLANK_INTERVAL;
    if (m_serial_pause)
    {
        m_serial->Synchronize();
        return 0.001;
    }

    // framelimiter on?
    double sleep_time;
    if (m_frame_limiter)
    {
        // using "accurate" timing?
        Timer exec_timer;
        uint64 clocks_executed;
        if (m_accurate_timing)
        {
            // determine the number of cycles we should be at
            double frame_start_time = m_reset_timer.GetTimeSeconds();
            uint64 target_clocks = TimeToClocks(frame_start_time);
            uint64 current_clocks = m_clocks_since_reset;

            // check that we're not ahead (is perfectly possible since each instruction takes a minimum of 4 clocks)
            if (target_clocks > current_clocks)
            {
                // keep executing until we meet our target
                clocks_executed = target_clocks - current_clocks;
                while (m_clocks_since_reset < target_clocks && !m_serial_pause)
                    Step();
            }
            else
            {
                clocks_executed = 0;
            }

            // find the number of clocks to next vblank
            uint64 next_vblank_clocks = m_last_vblank_clocks + 70224;
            uint64 sleep_clocks = (next_vblank_clocks > m_clocks_since_reset) ? (next_vblank_clocks - m_clocks_since_reset) : 0;
            sleep_time = ClocksToTime(sleep_clocks);

            // calculate the ideal time we want to hit the next frame
            double frame_end_time = m_reset_timer.GetTimeSeconds();
            double execution_time = frame_end_time - frame_start_time;
            TRACE("execution_time = %f, sleep time: %f", execution_time, sleep_time);
            sleep_time = Max(0.0, sleep_time - execution_time);
        }
        else
        {
            // execute one frame worth of cycles
            DebugAssert(m_clocks_since_reset <= 70224);
            uint64 target_clocks = uint64(70224 * m_speed_multiplier) - m_clocks_since_reset;
            m_clocks_since_reset = 0;
                                  
            // exec cycles
            while (m_clocks_since_reset < target_clocks && !m_serial_pause)
                Step();

            //Log_InfoPrintf("oc %u tc %u td %f", (uint32)m_clocks_since_reset, (uint32)target_clocks, m_reset_timer.GetTimeMilliseconds());
            m_clocks_since_reset -= (m_serial_pause) ? m_clocks_since_reset : target_clocks;
            clocks_executed = target_clocks;

            // calculate the sleep time
            sleep_time = Max((VBLANK_INTERVAL / m_speed_multiplier) - exec_timer.GetTimeSeconds(), 0.0);
        }
    }
    else
    {
        // framelimiter off, just execute as many as quickly as possible, say, 16ms worth at a time
        uint64 target_clocks = m_clocks_since_reset + 70224;
        while (m_clocks_since_reset < target_clocks && !m_serial_pause)
            Step();


        // don't sleep
        sleep_time = 0.0;
    }

    return sleep_time;
}

void System::CalculateCurrentSpeed()
{
    float diff = float(m_speed_timer.GetTimeSeconds());
    m_current_speed = float(m_cycles_since_speed_update) / (4194304 * diff);
    m_current_fps = float(m_frames_since_speed_update) / diff;
    m_cycles_since_speed_update = 0;
    m_frames_since_speed_update = 0;
    m_speed_timer.Reset();
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

void System::SetPadDirectionState(uint8 state)
{
    // flip on bits to off (which is what the gb expects)
    state = (state & PAD_DIRECTION_MASK) ^ PAD_DIRECTION_MASK;
    if (m_pad_direction_state == state)
        return;

    m_pad_direction_state = state;
    CPUInterruptRequest(CPU_INT_JOYPAD);
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

void System::SetPadButtonState(uint8 state)
{
    // flip on bits to off (which is what the gb expects)
    state = (state & PAD_BUTTON_MASK) ^ PAD_BUTTON_MASK;
    if (m_pad_button_state == state)
        return;

    m_pad_button_state = state;
    CPUInterruptRequest(CPU_INT_JOYPAD);
}

void System::SetTargetSpeed(float multiplier)
{
    m_speed_multiplier = multiplier;
    m_reset_timer.Reset();
    m_clocks_since_reset = 0;
    m_cycles_since_speed_update = 0;
    m_last_vblank_clocks = 0;

    m_speed_timer.Reset();
    m_cycles_since_speed_update = 0;
}

void System::SetFrameLimiter(bool on)
{
    m_frame_limiter = on; 
    m_reset_timer.Reset(); 
    m_clocks_since_reset = 0; 
    m_last_vblank_clocks = 0;

    m_speed_timer.Reset();
    m_cycles_since_speed_update = 0;
}

void System::SetAccurateTiming(bool on)
{
    m_accurate_timing = on;
    m_reset_timer.Reset();
    m_clocks_since_reset = 0;
    m_last_vblank_clocks = 0;

    m_speed_timer.Reset();
    m_cycles_since_speed_update = 0;
}

bool System::GetAudioEnabled() const
{
    return m_audio->GetOutputEnabled();
}

void System::SetAudioEnabled(bool enabled)
{
    m_audio->SetOutputEnabled(enabled);
}

bool System::LoadState(ByteStream *pStream, Error *pError)
{
    Timer loadTimer;

    // Create stream, load header
    BinaryReader binaryReader(pStream);
    uint32 saveStateVersion = binaryReader.ReadUInt32();
    if (saveStateVersion != SAVESTATE_LOAD_VERSION)
    {
        pError->SetErrorUserFormatted(1, "Save state version mismatch, expected %u, got %u", (uint32)SAVESTATE_LOAD_VERSION, saveStateVersion);
        return false;
    }

    // Read state
    m_mode = (SYSTEM_MODE)binaryReader.ReadUInt8();
    m_frame_counter = binaryReader.ReadUInt32();
    if (m_mode >= NUM_SYSTEM_MODES)
    {
        pError->SetErrorUserFormatted(1, "Corrupted save state.");
        return false;
    }

    // Read memory
    binaryReader.ReadBytes(m_memory_vram, sizeof(m_memory_vram));
    binaryReader.ReadBytes(m_memory_wram, sizeof(m_memory_wram));
    binaryReader.ReadBytes(m_memory_oam, sizeof(m_memory_oam));
    binaryReader.ReadBytes(m_memory_zram, sizeof(m_memory_zram));

    // Read registers
    m_vram_bank = binaryReader.ReadUInt8();
    m_high_wram_bank = binaryReader.ReadUInt8();
    m_memory_locked_cycles = binaryReader.ReadUInt32();
    m_timer_clocks  = binaryReader.ReadUInt32();
    m_timer_divider_clocks = binaryReader.ReadUInt32();
    m_timer_divider = binaryReader.ReadUInt8();
    m_timer_counter = binaryReader.ReadUInt8();
    m_timer_overflow_value = binaryReader.ReadUInt8();
    m_timer_control = binaryReader.ReadUInt8();
    m_pad_row_select = binaryReader.ReadUInt8();
    m_pad_direction_state = binaryReader.ReadUInt8();
    m_pad_button_state = binaryReader.ReadUInt8();
    m_cgb_speed_switch = binaryReader.ReadUInt8();
    m_biosLatch = binaryReader.ReadBool();
    m_vramLocked = binaryReader.ReadBool();
    m_oamLocked = binaryReader.ReadBool();
    if (pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Stream read error after restoring system.");
        return false;
    }

    // Read Cartridge state
    if (!m_cartridge->LoadState(pStream, binaryReader, pError))
        return false;
    if (pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Stream read error after restoring Cartridge.");
        return false;
    }

    // Read CPU state
    if (!m_cpu->LoadState(pStream, binaryReader, pError))
        return false;
    if (pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Stream read error after restoring CPU.");
        return false;
    }

    // Read Display state
    if (!m_display->LoadState(pStream, binaryReader, pError))
        return false;
    if (pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Stream read error after restoring display.");
        return false;
    }

    // Read Audio state
    if (!m_audio->LoadState(pStream, binaryReader, pError))
        return false;
    if (pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Stream read error after restoring audio.");
        return false;
    }

    // Read serial state
    if (!m_serial->LoadState(pStream, binaryReader, pError))
        return false;
    if (pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Stream read error after restoring serial.");
        return false;
    }

    // Done
    saveStateVersion = binaryReader.ReadUInt32();
    if (saveStateVersion != ~(uint32)SAVESTATE_LOAD_VERSION || pStream->InErrorState())
    {
        pError->SetErrorUserFormatted(1, "Error reading trailing signature.");
        return false;
    }

    // All good
    Log_DevPrintf("State loaded.");
    Log_ProfilePrintf("State load took %.4fms", loadTimer.GetTimeMilliseconds());
    return true;
}

bool System::SaveState(ByteStream *pStream)
{
    Timer saveTimer;

    // Create stream, write header
    BinaryWriter binaryWriter(pStream);
    binaryWriter.WriteUInt32(SAVESTATE_SAVE_VERSION);

    // Write state
    binaryWriter.WriteUInt8((uint8)m_mode);
    binaryWriter.WriteUInt32(m_frame_counter);

    // Write memory
    binaryWriter.WriteBytes(m_memory_vram, sizeof(m_memory_vram));
    binaryWriter.WriteBytes(m_memory_wram, sizeof(m_memory_wram));
    binaryWriter.WriteBytes(m_memory_oam, sizeof(m_memory_oam));
    binaryWriter.WriteBytes(m_memory_zram, sizeof(m_memory_zram));
    
    // Write registers
    binaryWriter.WriteUInt8(m_vram_bank);
    binaryWriter.WriteUInt8(m_high_wram_bank);
    binaryWriter.WriteUInt32(m_memory_locked_cycles);
    binaryWriter.WriteUInt32(m_timer_clocks);
    binaryWriter.WriteUInt32(m_timer_divider_clocks);
    binaryWriter.WriteUInt8(m_timer_divider);
    binaryWriter.WriteUInt8(m_timer_counter);
    binaryWriter.WriteUInt8(m_timer_overflow_value);
    binaryWriter.WriteUInt8(m_timer_control);
    binaryWriter.WriteUInt8(m_pad_row_select);
    binaryWriter.WriteUInt8(m_pad_direction_state);
    binaryWriter.WriteUInt8(m_pad_button_state);
    binaryWriter.WriteUInt8(m_cgb_speed_switch);
    binaryWriter.WriteBool(m_biosLatch);
    binaryWriter.WriteBool(m_vramLocked);
    binaryWriter.WriteBool(m_oamLocked);
    if (pStream->InErrorState())
        return false;

    // Write Cartridge state
    m_cartridge->SaveState(pStream, binaryWriter);
    if (pStream->InErrorState())
        return false;

    // Write CPU state
    m_cpu->SaveState(pStream, binaryWriter);
    if (pStream->InErrorState())
        return false;

    // Write Display state
    m_display->SaveState(pStream, binaryWriter);
    if (pStream->InErrorState())
        return false;

    // Write Audio state
    m_audio->SaveState(pStream, binaryWriter);
    if (pStream->InErrorState())
        return false;

    // Write Serial state
    m_serial->SaveState(pStream, binaryWriter);
    if (pStream->InErrorState())
        return false;

    // Write trailing signature
    binaryWriter.WriteUInt32(~(uint32)SAVESTATE_SAVE_VERSION);
    if (binaryWriter.InErrorState())
        return false;

    // All good
    Log_DevPrintf("State saved.");
    Log_ProfilePrintf("State save took %.4fms", saveTimer.GetTimeMilliseconds());
    return true;
}

void System::DisableCPU(bool disabled)
{
    m_cpu->Disable(disabled);
}

void System::OAMDMATransfer(uint16 source_address)
{
    m_memory_locked_cycles = 0;
    
    // select locked memory range
    switch (source_address & 0xF000)
    {
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
        m_memory_locked_start = 0x0000;
        m_memory_locked_end = 0x3FFF;
        break;

    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
        m_memory_locked_start = 0x4000;
        m_memory_locked_end = 0x7FFF;
        break;

    case 0xA000:
    case 0xB000:
        m_memory_locked_start = 0xA000;
        m_memory_locked_end = 0xBFFF;
        break;

    case 0xC000:
        m_memory_locked_start = 0xC000;
        m_memory_locked_end = 0xCFFF;
        break;

    case 0xD000:
        m_memory_locked_start = 0xD000;
        m_memory_locked_end = 0xDFFF;
        break;

    case 0xE000:
        // TODO: This is shadow of C000-CFFF
        m_memory_locked_start = 0xE000;
        m_memory_locked_end = 0xEFFF;
        break;

    case 0xF000:
        {
            if (source_address < 0xFE00)
            {
                m_memory_locked_start = 0xF000;
                m_memory_locked_end = 0xFD99;
            }
        }
        break;
    }

    // Allow transfers from vram regardless of locking state.
    // Is this correct?
    bool vramLocked = m_vramLocked;

    if (source_address == 0xFE00)
    {
        // OAM-OAM - ignore?
        Log_WarningPrintf("DMA transfer from OAM-OAM");
    }
    else if (source_address == 0xFF00)
    {
        // MMIO/ZRAM->OAM - copy zeros?
        for (uint32 i = 0; i < 160; i++)
            m_memory_oam[i] = 0;
    }
    else
    {
        // slow but due to the ranges has to be done this way
        // TODO: break this up for reads/writes when in progress
        for (uint32 i = 0; i < 160; i++)
            m_memory_oam[i] = CPURead(source_address + (uint16)i);
    }

    // Stall memory access for ~160 microseconds
    m_vramLocked = vramLocked;
    m_memory_locked_cycles = 640;
    UpdateNextEventCycle();
}

bool System::SwitchCGBSpeed()
{
    if (!(m_cgb_speed_switch & (1 << 0)))
        return false;

    // synchronize all clocks at the current clock speed
    m_display->Synchronize();
    m_audio->Synchronize();
    m_serial->Synchronize();
    SynchronizeTimers();

    // Flips the switch bit off at the same time.
    m_cgb_speed_switch ^= 0x81;
    if (m_cgb_speed_switch & 0x80)
        Log_DevPrintf("Switching to CGB double speed mode.");
    else
        Log_DevPrintf("Switching to normal speed mode.");

    // reset timing - so that accurate timing doesn't break
    //m_clocks_since_reset = 0;
    //m_reset_timer.Reset();

    // re-synchronize all clocks again to fix the cycle counters
    m_display->Synchronize();
    m_audio->Synchronize();
    m_serial->Synchronize();
    SynchronizeTimers();
    UpdateNextEventCycle();    
    return true;
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
    Y_memzero(m_memory_ioreg, sizeof(m_memory_ioreg));

    // pad
    m_pad_row_select = 0;
}

void System::ResetTimer()
{
    m_timer_last_cycle = 0;
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

void System::SynchronizeTimers()
{
    uint32 cycles_to_execute = CalculateDoubleSpeedCycleCount(m_timer_last_cycle);
    m_timer_last_cycle = GetCycleNumber();

    // cpu runs at 4,194,304hz
    // timer runs at 16,384hz
    // therefore, every 256 cpu "clocks" equals one timer tick
    m_timer_divider_clocks += cycles_to_execute;
    while (m_timer_divider_clocks >= 256)
    {
        m_timer_divider++;
        m_timer_divider_clocks -= 256;
    }

    // timer start/stop
    if (m_timer_control & 0x4)
    {
        // add cycles
        m_timer_clocks += cycles_to_execute;

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

    ScheduleTimerSynchronization();
}

void System::ScheduleTimerSynchronization()
{
    if (m_timer_control & 0x4)
    {
        // schedule update for the next interrupt time
        static const uint32 clocks_per_timer_ticks[] = { 1024, 16, 64, 256 };
        uint32 clocks_per_timer_tick = clocks_per_timer_ticks[m_timer_control & 0x3];
        uint32 next_interrupt_time = (256 - m_timer_counter) * clocks_per_timer_tick - m_timer_clocks;
        SetNextTimerSyncCycle(next_interrupt_time);
    }
    else
    {
        // divider timer is updated on-demand when it is read, so just set to +1 sec
        SetNextTimerSyncCycle(4194304);
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

uint8 System::CPURead(uint16 address)
{
//     if (address == 0xc009)
//         __debugbreak();

    // when DMA transfer is in progress, all memory except FF80-FFFE is inaccessible
    if (m_memory_locked_cycles > 0 && !m_memory_permissive && address >= m_memory_locked_start && address <= m_memory_locked_end)
    {
        // TODO: Should change the currently-buffered byte in the DMA transfer
        Log_DevPrintf("WARN: CPU read of address 0x%04X denied during DMA transfer", address);
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
            if (m_biosLatch)
            {
                // DMG rom is 256 bytes from 0000->00FF
                // CGB rom is 256 bytes from 0000->00FF, 0200->08FF
                if (m_mode == SYSTEM_MODE_DMG)
                {
                    if (address <= 0x00FF)
                        return m_bios[address];
                }
                else if (m_mode == SYSTEM_MODE_CGB)
                {
                    if (address <= 0x00FF)
                        return m_bios[address];
                    else if (address >= 0x0200 && address <= 0x08FF)
                        return m_bios[0x0100 + (address - 0x0200)];
                }
            }

            // Cart read
            return (m_cartridge != nullptr) ? m_cartridge->CPURead(address) : 0x00;
        }

        // video memory
    case 0x8000:
    case 0x9000:
        {
            m_display->Synchronize();
            if (m_vramLocked && !m_memory_permissive)
            {
                // Apparently returns 0xFF?
                Log_DevPrintf("WARN: CPU read of VRAM address 0x%04X while locked.", address);
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
                    m_display->Synchronize();
                    if (m_oamLocked && !m_memory_permissive)
                    {
                        // Apparently returns 0xFF?
                        Log_DevPrintf("WARN: CPU read of OAM address 0x%04X while locked.", address);
                        return 0xFF;
                    }
                    else if (address >= 0xFEA0)
                    {
                        Log_DevPrintf("WARN: Out-of-range read of OAM address 0x%04X", address);
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
//     if (address == 0xd000)
//         __debugbreak();

    // when DMA transfer is in progress, all memory except FF80-FFFE is inaccessible
    if (m_memory_locked_cycles > 0 && !m_memory_permissive && address >= m_memory_locked_start && address <= m_memory_locked_end)
    {
        Log_DevPrintf("WARN: CPU write of address 0x%04X (value 0x%02X) denied during DMA transfer", address, value);
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
            m_display->Synchronize();
            if (m_vramLocked && !m_memory_permissive)
            {
                Log_DevPrintf("WARN: CPU write of VRAM address 0x%04X (value 0x%02X) while locked.", address, value);
                return;
            }

//             if (address >= 0x9800 && m_vram_bank == 1)
//             {
//                 uint32 tx = (address - 0x9800) % 32;
//                 uint32 ty = (address - 0x9800) / 32;
//                 Log_DevPrintf("tile update: (%u, %u) -> %u", tx, ty, value);
//             }

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
                    m_display->Synchronize();
                    if (m_oamLocked && !m_memory_permissive)
                    {
                        // Apparently returns 0xFF?
                        Log_DevPrintf("WARN: CPU write of OAM address 0x%04X (value 0x%02X) while locked.", address, value);
                        return;
                    }
                    else if (address >= 0xFEA0)
                    {
                        Log_DevPrintf("WARN: Out-of-range write of OAM address 0x%04X (value 0x%02X)", address, value);
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

uint8 System::CPUReadIORegister(uint8 index)
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

                // FF01 - SB serial data
            case 0x01:
                m_serial->Synchronize();
                return m_serial->GetSerialData();

                // FF02 - SC serial control
            case 0x02:
                m_serial->Synchronize();
                return m_serial->GetSerialControl();

                // FF04 - DIV - Divider Register (R/W)
            case 0x04:
                SynchronizeTimers();
                return m_timer_divider;

                // FF05 - TIMA - Timer counter (R/W)
            case 0x05:
                SynchronizeTimers();
                return m_timer_counter;

                // FF06 - TMA - Timer Modulo (R/W)
            case 0x06:
                SynchronizeTimers();
                return m_timer_overflow_value;

                // FF07 - TAC - Timer Control (R/W)
            case 0x07:
                SynchronizeTimers();
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
            case 0x05:      // FF15 - NR?? - ???
            case 0x06:      // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
            case 0x07:      // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
            case 0x08:      // FF18 - NR23 - Channel 2 Frequency lo data (W)
            case 0x09:      // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
            case 0x0A:      // FF1A - NR30 - Channel 3 Sound on/off (R/W)
            case 0x0B:      // FF1B - NR31 - Channel 3 Sound Length
            case 0x0C:      // FF1C - NR32 - Channel 3 Select output level (R/W)
            case 0x0D:      // FF1D - NR33 - Channel 3 Frequency's lower data (W)
            case 0x0E:      // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
            case 0x0F:      // FF1F - NR?? - ???
                m_audio->Synchronize();
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
            case 0x07:      // FF27 - ???
            case 0x08:      // FF28 - ???
            case 0x09:      // FF29 - ???
            case 0x0A:      // FF2A - ???
            case 0x0B:      // FF2B - ???
            case 0x0C:      // FF2C - ???
            case 0x0D:      // FF2D - ???
            case 0x0E:      // FF2E - ???
            case 0x0F:      // FF2F - ???
                m_audio->Synchronize();
                return m_audio->CPUReadRegister(index);
            }

            break;
        }

    case 0x30:
        {
            // FF30-FF3F - Wave Pattern RAM
            m_audio->Synchronize();
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
                m_display->Synchronize();
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
                    m_display->Synchronize();
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
                    m_display->Synchronize();
                    return m_display->CPUReadRegister(index);
                }

                break;
            }

        case 0x70:
            {
                switch (index & 0x0F)
                {
                case 0x00:      // FF70 - SVBK - CGB Mode Only - WRAM Bank
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
    //return 0x00;
    return m_memory_ioreg[index];
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
                m_serial->Synchronize();
                m_serial->SetSerialData(value);
                return;

                // FF02 - SC serial control
            case 0x02:
                m_serial->Synchronize();
                m_serial->SetSerialControl(value);
                return;

                // FF04 - DIV - Divider Register (R/W)
            case 0x04:
                SynchronizeTimers();
                m_timer_divider = 0;
                return;

                // FF05 - TIMA - Timer counter (R/W)
            case 0x05:
                SynchronizeTimers();
                m_timer_counter = value;
                ScheduleTimerSynchronization();
                return;

                // FF06 - TMA - Timer Modulo (R/W)
            case 0x06:
                SynchronizeTimers();
                m_timer_overflow_value = value;
                ScheduleTimerSynchronization();
                return;

                // FF07 - TAC - Timer Control (R/W)
            case 0x07:
                SynchronizeTimers();
                m_timer_control = value;
                ScheduleTimerSynchronization();
                return;

                // interrupt flag
            case 0x0F:
                m_serial->Synchronize();
                m_display->Synchronize();
                SynchronizeTimers();
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
            case 0x05:      // FF15 - NR?? - ???
            case 0x06:      // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
            case 0x07:      // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
            case 0x08:      // FF18 - NR23 - Channel 2 Frequency lo data (W)
            case 0x09:      // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
            case 0x0A:      // FF1A - NR30 - Channel 3 Sound on/off (R/W)
            case 0x0B:      // FF1B - NR31 - Channel 3 Sound Length
            case 0x0C:      // FF1C - NR32 - Channel 3 Select output level (R/W)
            case 0x0D:      // FF1D - NR33 - Channel 3 Frequency's lower data (W)
            case 0x0E:      // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
            case 0x0F:      // FF1F - NR?? - ???
                m_audio->Synchronize();
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
            case 0x07:      // FF27 - ???
            case 0x08:      // FF28 - ???
            case 0x09:      // FF29 - ???
            case 0x0A:      // FF2A - ???
            case 0x0B:      // FF2B - ???
            case 0x0C:      // FF2C - ???
            case 0x0D:      // FF2D - ???
            case 0x0E:      // FF2E - ???
            case 0x0F:      // FF2F - ???
                m_audio->Synchronize();
                m_audio->CPUWriteRegister(index, value);
                return;
            }

            break;
        }

    case 0x30:
        {
            // FF30-FF3F - Wave Pattern RAM
            m_audio->Synchronize();
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
                m_display->Synchronize();
                m_display->CPUWriteRegister(index, value);
                return;

            case 0x06:      // FF46 - DMA - DMA Transfer and Start Address (W)
                {
                    m_display->Synchronize();

                    // Writing to this register launches a DMA transfer from ROM or RAM to OAM memory (sprite attribute table). The written value specifies the transfer source address divided by 100h
                    // It takes 160 microseconds until the transfer has completed (80 microseconds in CGB Double Speed Mode), during this time the CPU can access only HRAM (memory at FF80-FFFE).
                    uint16 source_address = (uint16)value * 256;
                    OAMDMATransfer(source_address);
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
                    m_cgb_speed_switch = (m_cgb_speed_switch & 0xFE) | (value & 0x01);
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
                    m_display->Synchronize();
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
                    m_display->Synchronize();
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
                    m_high_wram_bank = value & 0x07;

                    // Writing a value of 01h-07h will select Bank 1-7, writing a value of 00h will select Bank 1 either.
                    if (m_high_wram_bank == 0)
                        m_high_wram_bank = 1;

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
    m_memory_ioreg[index] = value;
}

void System::CPUInterruptRequest(uint8 index)
{
    TRACE("CPU raise interrupt %u", index);
    m_cpu->RaiseInterrupt(index);
}
