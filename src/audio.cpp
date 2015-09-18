#include "audio.h"
#include "system.h"
#include "YBaseLib/Log.h"
Log_SetChannel(Audio);

Audio::Audio(System *system)
    : m_system(system)
{

}

Audio::~Audio()
{

}

void Audio::Reset()
{
    Y_memzero(&m_registers, sizeof(m_registers));
}

uint8 Audio::CPUReadRegister(uint8 index) const
{
    switch (index)
    {
    case 0x10:      // FF10 - NR10 - Channel 1 Sweep register (R/W)
        return m_registers.NR10;

    case 0x11:      // FF11 - NR11 - Channel 1 Sound length/Wave pattern duty (R/W)
        return m_registers.NR11;

    case 0x12:      // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
        return m_registers.NR12;

    case 0x13:      // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
        return m_registers.NR13;

    case 0x14:      // FF14 - NR14 - Channel 1 Frequency hi (R/W)
        return m_registers.NR14;

    case 0x16:      // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
        return m_registers.NR21;

    case 0x17:      // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
        return m_registers.NR22;

    case 0x18:      // FF18 - NR23 - Channel 2 Frequency lo data (W)
        return m_registers.NR23;

    case 0x19:      // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
        return m_registers.NR24;

    case 0x1A:      // FF1A - NR30 - Channel 3 Sound on/off (R/W)
        return m_registers.NR30;

    case 0x1B:      // FF1B - NR31 - Channel 3 Sound Length
        return m_registers.NR31;

    case 0x1C:      // FF1C - NR32 - Channel 3 Select output level (R/W)
        return m_registers.NR32;

    case 0x1D:      // FF1D - NR33 - Channel 3 Frequency's lower data (W)
        return m_registers.NR33;
       
    case 0x1E:      // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
        return m_registers.NR34;

    case 0x20:      // FF20 - NR41 - Channel 4 Sound Length (R/W)
        return m_registers.NR41;

    case 0x21:      // FF21 - NR42 - Channel 4 Volume Envelope (R/W)
        return m_registers.NR42;

    case 0x22:      // FF22 - NR43 - Channel 4 Polynomial Counter (R/W)
        return m_registers.NR43;

    case 0x23:      // FF23 - NR44 - Channel 4 Counter/consecutive; Inital (R/W)
        return m_registers.NR44;

    case 0x24:      // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
        return m_registers.NR50;

    case 0x25:      // FF25 - NR51 - Selection of Sound output terminal (R/W)
        return m_registers.NR51;

    case 0x26:      // FF26 - NR52 - sound on/off
        return m_registers.NR52;
    }

    if (index >= 0x30 && index <= 0x3F)
    {
        // Wave pattern ram
        return m_registers.WAVE_PATTERN[index - 0x30];
    }

    Log_WarningPrintf("Unhandled CPU register read: 0x%02X", index);
    return 0x00;
}

void Audio::CPUWriteRegister(uint8 index, uint8 value)
{
    switch (index)
    {
    case 0x10:      // FF10 - NR10 - Channel 1 Sweep register (R/W)
        m_registers.NR10 = value;
        return;

    case 0x11:      // FF11 - NR11 - Channel 1 Sound length/Wave pattern duty (R/W)
        m_registers.NR11 = value;
        return;

    case 0x12:      // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
        m_registers.NR12 = value;
        return;

    case 0x13:      // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
        m_registers.NR13 = value;
        return;

    case 0x14:      // FF14 - NR14 - Channel 1 Frequency hi (R/W)
        m_registers.NR14 = value;
        return;

    case 0x16:      // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
        m_registers.NR21 = value;
        return;

    case 0x17:      // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
        m_registers.NR22 = value;
        return;

    case 0x18:      // FF18 - NR23 - Channel 2 Frequency lo data (W)
        m_registers.NR23 = value;
        return;

    case 0x19:      // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
        m_registers.NR24 = value;
        return;

    case 0x1A:      // FF1A - NR30 - Channel 3 Sound on/off (R/W)
        m_registers.NR30 = value;
        return;

    case 0x1B:      // FF1B - NR31 - Channel 3 Sound Length
        m_registers.NR31 = value;
        return;

    case 0x1C:      // FF1C - NR32 - Channel 3 Select output level (R/W)
        m_registers.NR32 = value;
        return;

    case 0x1D:      // FF1D - NR33 - Channel 3 Frequency's lower data (W)
        m_registers.NR33 = value;
        return;
       
    case 0x1E:      // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
        m_registers.NR34 = value;
        return;

    case 0x20:      // FF20 - NR41 - Channel 4 Sound Length (R/W)
        m_registers.NR41 = value;
        return;

    case 0x21:      // FF21 - NR42 - Channel 4 Volume Envelope (R/W)
        m_registers.NR42 = value;
        return;

    case 0x22:      // FF22 - NR43 - Channel 4 Polynomial Counter (R/W)
        m_registers.NR43 = value;
        return;

    case 0x23:      // FF23 - NR44 - Channel 4 Counter/consecutive; Inital (R/W)
        m_registers.NR44 = value;
        return;

    case 0x24:      // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
        m_registers.NR50 = value;
        return;

    case 0x25:      // FF25 - NR51 - Selection of Sound output terminal (R/W)
        m_registers.NR51 = value;
        return;

    case 0x26:      // FF26 - NR52 - sound on/off
        m_registers.NR52 = value;
        return;
    }

    if (index >= 0x30 && index <= 0x3F)
    {
        // Wave pattern ram
        m_registers.WAVE_PATTERN[index - 0x30] = value;
        Log_DevPrintf("WP ind %u - %u", index - 0x30, value);
        return;
    }

    Log_WarningPrintf("Unhandled CPU register write: 0x%02X (value 0x%02X)", index, value);
}

