#pragma once
#include "YBaseLib/Assert.h"

template<typename TYPE>
class AudioBuffer
{
public:
    AudioBuffer(size_t sampleCount);
    ~AudioBuffer();

    void PutSample(TYPE sample);
    void GetSamples(TYPE *dest, size_t count);

private:
    TYPE *m_buffer;
    size_t m_sampleCount;
    size_t m_readPos;
    size_t m_writePos;
};

template<typename TYPE>
AudioBuffer<TYPE>::AudioBuffer(size_t length)
{
    m_buffer = new TYPE[length];
    Y_memzero(m_buffer, sizeof(TYPE) * length);
    m_sampleCount = length;
    m_readPos = 0;
    m_writePos = 0;
}

template<typename TYPE>
AudioBuffer<TYPE>::~AudioBuffer()
{
    delete[] m_buffer;
}

template<typename TYPE>
void AudioBuffer<TYPE>::GetSamples(TYPE *dest, size_t count)
{
    DebugAssert(count <= m_sampleCount);
    
    size_t i;
    for (i = 0; i < count; i++)
    {
        if (m_readPos == m_writePos)
            break;

        dest[i] = m_buffer[m_readPos++];
        m_readPos %= m_sampleCount;
    }

    for (; i < count; i++)
        dest[i] = (TYPE)0;
}

template<typename TYPE>
void AudioBuffer<TYPE>::PutSample(TYPE sample)
{
    m_buffer[m_writePos++] = sample;
    m_writePos %= m_sampleCount;
}
