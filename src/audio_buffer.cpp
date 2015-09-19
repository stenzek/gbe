#include "audio_buffer.h"
#include "YBaseLib/Log.h"
Log_SetChannel(AudioBuffer);

AudioBuffer::AudioBuffer(size_t length)
{
    m_buffer = new SampleType[length];
    Y_memzero(m_buffer, sizeof(SampleType) * length);
    m_sampleCount = length;
    m_readPos = 0;
    m_writePos = 0;
    m_readOverrun = false;
    m_writeOverrun = false;
}

AudioBuffer::~AudioBuffer()
{
    delete[] m_buffer;
}

size_t AudioBuffer::GetAvailableSamples() const
{
    if (m_writePos > m_readPos)
        return m_writePos - m_readPos;
    else
        return (m_sampleCount - m_readPos) + m_writePos;
}

void AudioBuffer::GetSamples(SampleType *dest, size_t count)
{
    DebugAssert(count <= m_sampleCount);

    if (m_writeOverrun)
        m_writeOverrun = false;
    
//     size_t i;
//     for (i = 0; i < count; i++)
//     {
//         if (m_readPos == m_writePos)
//         {
//             Log_WarningPrintf("Read overrun");
//             //m_readOverrun = true;
//             break;
//         }
// 
//         dest[i] = m_buffer[m_readPos++];
//         m_readPos %= m_sampleCount;
//     }

    while (count > 0)
    {
        size_t avail = m_sampleCount - m_readPos;
        if (avail > count)
            avail = count;

        Y_memcpy(dest, m_buffer + m_readPos, avail * 2);
        m_readPos += avail;
        m_readPos %= m_sampleCount;
        dest += avail;
        count -= avail;
    }

    //Log_DevPrintf("Get %u samples, readpos %u, writepos %u", count, m_readPos, m_writePos);
}

void AudioBuffer::PutSamples(const SampleType *samples, size_t count)
{
    if (m_readOverrun)
    {
        m_readOverrun = false;
        m_readPos = m_writePos;
    }

//     for (size_t i = 0; i < count; i++)
//     {
//         if (m_writeOverrun)
//             m_readPos++;
// 
//         m_buffer[m_writePos++] = samples[i];
//         m_writePos %= m_sampleCount;
// 
//         if (m_writePos == m_readPos)
//         {
//             Log_WarningPrintf("Write overrun");
//             m_writeOverrun = true;
//         }
//     }

    while (count > 0)
    {
        size_t space = (m_sampleCount - m_writePos);
        if (space > count)
            space = count;

        if (m_writeOverrun)
        {
            m_readPos += space;
            m_readPos %= m_sampleCount;
        }

        Y_memcpy(m_buffer + m_writePos, samples, space * 2);
        m_writePos += space;
        m_writePos %= m_sampleCount;
        samples += space;
        count -= space;

        if (m_writePos == m_readPos)
        {
            Log_WarningPrintf("Write overrun");
            m_writeOverrun = true;
        }
    }

    //Log_DevPrintf("Put %u samples, readpos %u, writepos %u", count, m_readPos, m_writePos);
}
