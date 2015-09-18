#include "audio.h"
#include "system.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/MutexLock.h"
#include "Multi_Buffer.h"
#include "Gb_Apu.h"
Log_SetChannel(Audio);

static Timer tmr;

Audio::Audio(System *system)
    : m_system(system)
    , m_buffer(new Stereo_Buffer())
    , m_apu(new Gb_Apu())
    , m_audio_cycle(0)
    , m_cpu_cycle(0)
{
    m_buffer->clock_rate(4194304);
    m_buffer->set_sample_rate(44100, 1000);
    m_apu->set_output(m_buffer->center(), m_buffer->left(), m_buffer->right());
}

Audio::~Audio()
{
    delete m_apu;
    delete m_buffer;
}

void Audio::Reset()
{
    m_apu->reset((m_system->InCGBMode()) ? Gb_Apu::mode_cgb : Gb_Apu::mode_dmg, false);
}

uint32 Audio::GetAudioCycle() const
{
    //return m_audio_cycle;
    return (uint32)(tmr.GetTimeSeconds() * 4194304);
}

void Audio::ExecuteFor(uint32 cycles)
{

}

uint8 Audio::CPUReadRegister(uint8 index) const
{
    MutexLock lock(m_lock);
    return (uint8)m_apu->read_register(GetAudioCycle(), 0xFF00 | index);
}

void Audio::CPUWriteRegister(uint8 index, uint8 value)
{
    MutexLock lock(m_lock);
    return m_apu->write_register(GetAudioCycle(), 0xFF00 | index, value);
}

void Audio::EndFrame()
{
    MutexLock lock(m_lock);

    uint32 c = GetAudioCycle();

    m_apu->end_frame(c);
    m_buffer->end_frame(c);
    //m_audio_cycle = 0;

    tmr.Reset();
}

size_t Audio::GetSamplesAvailable() const
{
    MutexLock lock(m_lock);
    return m_buffer->samples_avail();
}

size_t Audio::ReadSamples(int16 *buffer, size_t count)
{
    MutexLock lock(m_lock);
    long read_count = m_buffer->read_samples((blip_sample_t *)buffer, count);
    return read_count;
}

