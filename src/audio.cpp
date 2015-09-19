#include "audio.h"
#include "audio_buffer.h"
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
    , m_queue(new AudioBuffer(11025 * 2))       // two second of queuing, stereo
{
    m_buffer->clock_rate(4194304);
    m_buffer->set_sample_rate(44100);
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
    m_audio_cycle = 0;
}

uint32 Audio::GetAudioCycle() const
{
    return m_audio_cycle;
}

void Audio::ExecuteFor(uint32 cycles)
{
    uint32 PUSH_FREQUENCY_IN_CYCLES = 8192;

    m_audio_cycle += cycles;

    while (m_audio_cycle >= PUSH_FREQUENCY_IN_CYCLES)
    {
        m_audio_cycle -= PUSH_FREQUENCY_IN_CYCLES;

        // push a frame
        m_apu->end_frame(PUSH_FREQUENCY_IN_CYCLES);
        m_buffer->end_frame(PUSH_FREQUENCY_IN_CYCLES);
        m_audio_cycle = 0;
        tmr.Reset();

        // copy to output buffer
        //if (m_buffer->samples_avail() >= 2048)
        {
            m_lock.Lock();
            {
                long samples_available = m_buffer->samples_avail();
                while (samples_available > 0)
                {
                    // TODO optimize me..
                    AudioBuffer::SampleType samples[2048];
                    long samples_pull = Min((long)countof(samples), samples_available);
                    samples_pull = m_buffer->read_samples(samples, samples_pull);
                    m_queue->PutSamples(samples, samples_pull);
                    samples_available -= samples_pull;
                }
            }
            m_lock.Unlock();
        }
    }
}

uint8 Audio::CPUReadRegister(uint8 index) const
{
    return (uint8)m_apu->read_register(GetAudioCycle(), 0xFF00 | index);
}

void Audio::CPUWriteRegister(uint8 index, uint8 value)
{
    return m_apu->write_register(GetAudioCycle(), 0xFF00 | index, value);
}

size_t Audio::ReadSamples(int16 *buffer, size_t count)
{
    MutexLock lock(m_lock);

    count = Min(count, m_queue->GetAvailableSamples());
    m_queue->GetSamples(buffer, count);
    return count;
}

