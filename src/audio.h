#pragma once
#include "structures.h"
#include "audio_buffer.h"

class System;

class Audio
{
public:
    Audio(System *system);
    ~Audio();

    void Reset();

    // register access
    uint8 CPUReadRegister(uint8 index) const;
    void CPUWriteRegister(uint8 index, uint8 value);

private:
    struct Registers
    {
        uint8 NR10;
        uint8 NR11;
        uint8 NR12;
        uint8 NR13;
        uint8 NR14;
        uint8 NR21;
        uint8 NR22;
        uint8 NR23;
        uint8 NR24;
        uint8 NR30;
        uint8 NR31;
        uint8 NR32;
        uint8 NR33;
        uint8 NR34;
        uint8 NR40;
        uint8 NR41;
        uint8 NR42;
        uint8 NR43;
        uint8 NR44;
        uint8 NR50;
        uint8 NR51;
        uint8 NR52;

        uint8 WAVE_PATTERN[16];
    };

    System *m_system;

    Registers m_registers;

};