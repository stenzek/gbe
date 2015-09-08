#pragma once
#include "YBaseLib/Common.h"

class ByteStream;
class Error;

class Cartridge
{
public:
    static const uint32 ROM0_SIZE = 0x4000;
    static const uint32 ROM1_SIZE = 0x4000;

public:
    Cartridge();
    ~Cartridge();

    const byte *GetROM0() const { return m_ROM0; }
    const byte *GetROM1() const { return m_ROM1; }
    const bool HasROM1() const { return (m_ROM1 != nullptr); }

    bool Load(ByteStream *pStream, Error *pError);

private:
    byte *m_ROM0;
    byte *m_ROM1;
};
