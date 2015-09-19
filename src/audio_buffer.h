#pragma once
#include "YBaseLib/Assert.h"

class AudioBuffer
{
public:
    typedef int16 SampleType;

public:
    AudioBuffer(size_t sampleCount);
    ~AudioBuffer();

    size_t GetAvailableSamples() const;
    void PutSamples(const SampleType *samples, size_t count);
    void GetSamples(SampleType *dest, size_t count);

private:
    SampleType *m_buffer;
    size_t m_sampleCount;
    size_t m_readPos;
    size_t m_writePos;
    bool m_readOverrun;
    bool m_writeOverrun;
};
