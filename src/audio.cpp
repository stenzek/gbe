#include "audio.h"
#include "system.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/MutexLock.h"
#include "Multi_Buffer.h"
#include "Gb_Apu.h"
Log_SetChannel(Audio);

static const uint32 OUTPUT_BUFFER_SIZE = 11025 * 2; // 0.25 seconds output buffering (stereo)
static const uint32 PUSH_FREQUENCY_IN_CYCLES = 8192;

Audio::Audio(System *system)
    : m_system(system)
    , m_buffer(new Stereo_Buffer())
    , m_apu(new Gb_Apu())
    , m_audio_cycle(0)
    , m_output_buffer(new int16[OUTPUT_BUFFER_SIZE])
    , m_output_buffer_rpos(0)
    , m_output_buffer_wpos(0)
    , m_output_buffer_read_overrun(false)
    , m_output_buffer_write_overrun(false)
    , m_output_enabled(true)
{
    m_buffer->clock_rate(4194304);
    m_buffer->set_sample_rate(44100);
    m_apu->set_output(m_buffer->center(), m_buffer->left(), m_buffer->right());
}

Audio::~Audio()
{
    delete[] m_output_buffer;
    delete m_apu;
    delete m_buffer;
}

void Audio::SetOutputEnabled(bool enabled)
{
    if (m_output_enabled == enabled)
        return;

    if (enabled)
    {
        m_buffer->clear();
        m_apu->set_output(m_buffer->center(), m_buffer->left(), m_buffer->right());
        m_output_buffer_rpos = 0;
        m_output_buffer_wpos = 0;
        m_output_buffer_read_overrun = false;
        m_output_buffer_write_overrun = false;
        m_output_enabled = true;
    }
    else
    {
        m_buffer->end_frame(PUSH_FREQUENCY_IN_CYCLES);
        m_apu->set_output(nullptr);
        m_output_enabled = false;
    }

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
    m_audio_cycle += cycles;

    while (m_audio_cycle >= PUSH_FREQUENCY_IN_CYCLES)
    {
        m_audio_cycle -= PUSH_FREQUENCY_IN_CYCLES;

        // push a frame
        m_apu->end_frame(PUSH_FREQUENCY_IN_CYCLES);

        // copy to output buffer
        if (m_output_enabled)
        {
            m_buffer->end_frame(PUSH_FREQUENCY_IN_CYCLES);

            m_lock.Lock();
            {
                // no more read buffer overrun
                m_output_buffer_read_overrun = false;

                // gather samples
                size_t remaining = m_buffer->samples_avail();
                while (remaining > 0)
                {
                    // find available space
                    size_t copy_samples = Min(remaining, OUTPUT_BUFFER_SIZE - m_output_buffer_wpos);
                    if (!m_output_buffer_write_overrun && m_output_buffer_rpos > m_output_buffer_wpos && (m_output_buffer_wpos + copy_samples) > m_output_buffer_rpos)
                    {
                        // don't spam about overruns with frame limiter off (it's guaranteed to happen)
                        if (m_system->GetFrameLimiter() && m_system->GetTargetSpeed() == 1.0f)
                            Log_WarningPrintf("Audio buffer overrun by write (too much data)");

                        m_output_buffer_write_overrun = true;
                    }

                    // copy samples
                    m_buffer->read_samples(m_output_buffer + m_output_buffer_wpos, copy_samples);
                    m_output_buffer_wpos += copy_samples;
                    m_output_buffer_wpos %= OUTPUT_BUFFER_SIZE;
                    remaining -= copy_samples;

                    // if there's a write overrun, start the next read at the end of the stuff we just wrote
                    if (m_output_buffer_write_overrun)
                        m_output_buffer_rpos = m_output_buffer_wpos;
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
    if (!m_output_enabled)
        return 0;

    MutexLock lock(m_lock);
    if (m_output_buffer_write_overrun)
    {
        // cancel the write overrun
        m_output_buffer_write_overrun = false;
    }
    else
    {
        // silence until we have at least a full buffer worth of samples to begin with
        size_t available_samples = (m_output_buffer_wpos > m_output_buffer_rpos) ? (m_output_buffer_wpos - m_output_buffer_rpos) : ((OUTPUT_BUFFER_SIZE - m_output_buffer_rpos) + m_output_buffer_wpos);
        if (available_samples < count)
            return 0;
    }

    size_t remaining = count;
    while (remaining > 0)
    {
        // check that we don't go past the write pointer
        size_t copy_samples = Min(remaining, OUTPUT_BUFFER_SIZE - m_output_buffer_rpos);
        if (!m_output_buffer_read_overrun && m_output_buffer_wpos > m_output_buffer_rpos && (m_output_buffer_rpos + copy_samples) > m_output_buffer_wpos)
        {
            Log_WarningPrintf("Audio buffer overrun by read (not enough data)");
            m_output_buffer_read_overrun = true;
        }

        // copy samples
        Y_memcpy(buffer, m_output_buffer + m_output_buffer_rpos, copy_samples * sizeof(int16));
        m_output_buffer_rpos += copy_samples;
        m_output_buffer_rpos %= OUTPUT_BUFFER_SIZE;
        buffer += copy_samples;
        remaining -= copy_samples;

        // if there's a read overrun, start the next write at the end of the stuff we just read
        if (m_output_buffer_read_overrun)
            m_output_buffer_wpos = m_output_buffer_rpos;
    }

    return count;
}

