#include "cartridge.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/Error.h"

Cartridge::Cartridge()
    : m_ROM0(nullptr),
      m_ROM1(nullptr)
{

}

Cartridge::~Cartridge()
{
    delete[] m_ROM1;
    delete[] m_ROM0;
}

bool Cartridge::Load(ByteStream *pStream, Error *pError)
{
    uint32 cartSize = (uint32)pStream->GetSize();

    // check size
    if (cartSize < ROM0_SIZE)
    {
        pError->SetErrorUser(1, "Cartridge must be at least ROM0_SIZE (16KB)");
        return false;
    }

    // read in rom0, always present
    m_ROM0 = new byte[ROM0_SIZE];
    if (!pStream->Read2(m_ROM0, ROM0_SIZE))
    {
        pError->SetErrorUser(1, "Failed to read ROM0 bank from cartridge");
        return false;
    }

    // check for rom1 first
    if (cartSize > ROM0_SIZE)
    {
        if (cartSize < (ROM0_SIZE + ROM1_SIZE))
        {
            pError->SetErrorUser(1, "Cartridge has extra data after ROM0 but not ROM1");
            return false;
        }

        m_ROM1 = new byte[ROM1_SIZE];
        if (!pStream->Read2(m_ROM1, ROM1_SIZE))
        {
            pError->SetErrorUser(1, "Failed to read ROM1 bank from cartridge");
            return false;
        }
    }

    return true;
}

