#pragma once
#include "structures.h"
#include "YBaseLib/Mutex.h"

class System;
class Gb_Apu;
class Stereo_Buffer;
class AudioBuffer;

class Audio
{
public:
    Audio(System *system);
    ~Audio();

    void Reset();
    void ExecuteFor(uint32 cycles);

    // register access
    uint8 CPUReadRegister(uint8 index) const;
    void CPUWriteRegister(uint8 index, uint8 value);

    // sample access
    size_t ReadSamples(int16 *buffer, size_t count);

private:
    uint32 GetAudioCycle() const;

    System *m_system;

    Gb_Apu *m_apu;
    Stereo_Buffer *m_buffer;

    uint32 m_audio_cycle;

    AudioBuffer *m_queue;
    Mutex m_lock;
};

