#include "system.h"
#include "cartridge.h"
#include "cpu.h"
#include "display.h"
#include "audio.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include "YBaseLib/Math.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryReadBuffer.h"
#include "YBaseLib/BinaryWriter.h"
#include "YBaseLib/BinaryWriteBuffer.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/Error.h"
#include <sys/types.h>
Log_SetChannel(System);

#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static const uint32 NW_VERSION = 1;

enum COMMAND
{
    COMMAND_HELLO,
    COMMAND_CLOCKRATE,
    COMMAND_DATA
};

// TODO: "Duplex buffer"

class ReadPacket : public BinaryReadBuffer
{
    ReadPacket() : BinaryReadBuffer(16) {}

public:
    virtual ~ReadPacket() {}

    const COMMAND GetPacketCommand() const { return m_command; }
    const uint32 GetPacketSize() const { return m_packetSize; }

    static ReadPacket *From(int fd)
    {
        ReadPacket *packet = new ReadPacket();

        int nbytes = 0;
        for (;;)
        {
            int result = recv(fd, (char *)packet->GetBufferPointer() + nbytes, packet->GetBufferSize() - nbytes, 0);
            if (result <= 0)
            {
                Log_ErrorPrintf("recv error: %d", WSAGetLastError());
                delete packet;
                return nullptr;
            }

            nbytes += result;
            if (nbytes < 3)
                continue;

            packet->m_command = (COMMAND)packet->ReadUInt8();
            packet->m_packetSize = (uint32)packet->ReadUInt16();
            if (nbytes < int(packet->m_packetSize + 3))
                continue;
            else
                break;
        }

        return packet;
    }

private:
    COMMAND m_command;
    uint32 m_packetSize;
};

class WritePacket : public BinaryWriteBuffer
{
public:
    WritePacket(COMMAND command) : BinaryWriteBuffer() 
    {
        WriteUInt8((uint8)command);
        WriteUInt16(0);
    }

    virtual ~WritePacket() {}

    bool Send(int fd)
    {
        uint64 offset = GetStreamPosition();
        SeekAbsolute(1);
        WriteUInt16((uint16)m_pStream->GetSize() - 3);
        SeekAbsolute(offset);

        int nbytes = 0;
        for (;;)
        {
            int result = send(fd, (const char *)GetBufferPointer() + nbytes, (int)m_pStream->GetSize() - nbytes, 0);
            if (result < 0)
            {
                Log_ErrorPrintf("send error: %d", WSAGetLastError());
                return false;
            }

            nbytes += result;
            if (nbytes == (int)m_pStream->GetSize())
                break;
        }

        return true;
    }
};

static void CleanupWinsock()
{
    WSACleanup();
}

static bool InitializeWinsock()
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        Log_ErrorPrint("Failed to initalize winsock");
        return false;
    }

    atexit(CleanupWinsock);
    return true;
}

void System::LinkInit()
{
    m_serial_control = 0x00;
    m_serial_data = 0xFF;
    m_linkClientSocket = -1;
    m_linkListenSocket = -1;
    m_linkExternalClockRate = 8192;
    m_linkWaitClocks = 0;
    m_linkSocketPollClocks = 0;
}

void System::LinkCleanup()
{
    if (m_linkClientSocket >= 0)
        closesocket(m_linkClientSocket);
    if (m_linkListenSocket >= 0)
        closesocket(m_linkListenSocket);
}

void System::LinkReset()
{
    m_serial_control = 0x00;
    m_serial_data = 0xFF;
    m_linkWaitClocks = 0;
    m_linkSocketPollClocks = 0;
}

bool System::LinkHost(uint32 port)
{
    if (!InitializeWinsock())
        return false;

    sockaddr_in listenaddr;
    Y_memzero(&listenaddr, sizeof(listenaddr));
    listenaddr.sin_family = AF_INET;
    listenaddr.sin_addr.s_addr = INADDR_ANY;
    listenaddr.sin_port = htons((uint16)port);
    
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
        Log_ErrorPrintf("socket() failed: %d", WSAGetLastError());
        return false;
    }

    if (bind(fd, (const sockaddr *)&listenaddr, sizeof(listenaddr)) != 0)
    {
        Log_ErrorPrintf("bind() failed: %d", WSAGetLastError());
        closesocket(fd);
        return false;
    }

    if (listen(fd, 1) != 0)
    {
        Log_ErrorPrintf("listen() failed: %d", WSAGetLastError());
        closesocket(fd);
        return false;
    }

    Log_InfoPrintf("Link hosting.");
    m_linkListenSocket = fd;
    return true;
}

bool System::LinkConnect(const char *host, uint32 port)
{
    if (!InitializeWinsock())
        return false;

    sockaddr_in sa;
    Y_memzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1)
    {
        Log_ErrorPrintf("inet_pton() failed: %d", WSAGetLastError());
        return false;
    }
    
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
        Log_ErrorPrintf("socket() failed: %d", WSAGetLastError());
        return false;
    }

    if (connect(fd, (const sockaddr *)&sa, sizeof(sa)) != 0)
    {
        Log_ErrorPrintf("connect() failed: %d", WSAGetLastError());
        closesocket(fd);
        return false;
    }

    // wait for hello
    {
        ReadPacket *hello = ReadPacket::From(fd);
        if (hello == nullptr)
        {
            closesocket(fd);
            return false;
        }

        uint32 ver = hello->ReadUInt32();
        if (ver != NW_VERSION)
        {
            Log_ErrorPrintf("Network version mismatch (server: %u, us: %u)", ver, NW_VERSION);
            closesocket(fd);
            return false;
        }
    }

    // send our hello
    {
        WritePacket hello(COMMAND_HELLO);
        hello.WriteUInt32(NW_VERSION);
        if (!hello.Send(fd))
        {
            closesocket(fd);
            return false;
        }
    }

    // send our clock rate
    {
        WritePacket clockrate(COMMAND_CLOCKRATE);
        clockrate.WriteUInt32(GetLinkClockRate());
        if (!clockrate.Send(fd))
        {
            closesocket(fd);
            return false;
        }
    }

    // ready
    m_linkClientSocket = fd;
    Log_InfoPrintf("Link socket connected.");
    return true;
}

void System::LinkAccept()
{
    sockaddr_storage sa;
    socklen_t length = sizeof(sa);
    int client_fd = accept(m_linkListenSocket, (sockaddr *)&sa, &length);
    if (client_fd < 0)
        return;

    SmallString str;
    inet_ntop(sa.ss_family, &sa, str.GetWriteableCharArray(), str.GetWritableBufferSize());
    str.UpdateSize();
    Log_DevPrintf("Link client connection accepted from '%s'", str.GetCharArray());

    if (m_linkClientSocket >= 0)
    {
        Log_ErrorPrintf("Closing client connection, already have client.");
        closesocket(client_fd);
        return;
    }

    // send hello
    {
        WritePacket packet(COMMAND_HELLO);
        packet.WriteUInt32(NW_VERSION);
        if (!packet.Send(client_fd))
        {
            closesocket(client_fd);
            return;
        }
    }

    // recv hello
    {
        ReadPacket *hello = ReadPacket::From(client_fd);
        if (hello == nullptr)
        {
            closesocket(client_fd);
            return;
        }

        uint32 ver = hello->ReadUInt32();
        if (ver != NW_VERSION)
        {
            Log_ErrorPrintf("Network version mismatch (client: %u, us: %u)", ver, NW_VERSION);
            closesocket(client_fd);
            return;
        }
    }

    // send our clockrate
    {
        WritePacket packet(COMMAND_CLOCKRATE);
        packet.WriteUInt32(GetLinkClockRate());
        if (!packet.Send(client_fd))
        {
            closesocket(client_fd);
            return;
        }
    }

    // ready to go
    m_linkClientSocket = client_fd;
    Log_InfoPrintf("Link connection established.");
}

void System::LinkTick(uint32 clocks)
{
    if (m_linkListenSocket < 0 && m_linkClientSocket < 0)
        return;

    // handle waits
    if (m_linkWaitClocks > 0)
    {
        if (clocks > m_linkWaitClocks)
        {
            m_linkWaitClocks = 0;
            m_serial_control &= ~(1 << 7);
            CPUInterruptRequest(CPU_INT_SERIAL);
        }
        else
        {
            m_linkWaitClocks -= clocks;
        }
    }

    // can we poll?
    if (clocks >= m_linkSocketPollClocks)
    {
        fd_set fdset;
        FD_ZERO(&fdset);

        int nfds = 0;
        if (m_linkListenSocket >= 0)
        {
            FD_SET((SOCKET)m_linkListenSocket, &fdset);
            nfds = m_linkListenSocket;
        }
        if (m_linkClientSocket >= 0)
        {
            FD_SET((SOCKET)m_linkClientSocket, &fdset);
            nfds = Max(m_linkListenSocket, nfds);
        }

        timeval tv = { 0, 0 };
        int active = select(nfds + 1, &fdset, nullptr, nullptr, &tv);
        if (active > 0)
        {
            if (FD_ISSET(m_linkListenSocket, &fdset))
                LinkAccept();
            if (FD_ISSET(m_linkClientSocket, &fdset))
                LinkRecv();
        }

        // find next interval
        // TODO: Accuracy here
        if ((m_serial_control & (1 << 0)))
            m_linkSocketPollClocks = 4194304 / GetLinkClockRate();
        else
            m_linkSocketPollClocks = 4194304 / m_linkExternalClockRate;
    }
    else
    {
        m_linkSocketPollClocks -= clocks;
    }    
}

uint32 System::GetLinkClockRate() const
{
    if (m_serial_control & (1 << 1) && InCGBMode())
        return 262144;
    else
        return 8192;
}

void System::LinkWait(uint32 clockrate)
{
    uint32 waitclocks = 4194304 / clockrate;
    m_linkWaitClocks = waitclocks;
}

void System::LinkSetControl(uint8 value)
{
    Log_DevPrintf("Serial control set to 0x%02X", value);

    m_serial_control = value;
    if (value & (1 << 7))
    {
        // start transfer
        // are we the server?
        if (value & (1 << 0))
        {
            Log_DevPrintf("Sending serial data: 0x%02X", m_serial_data);
            LinkSend(m_serial_data);
            LinkWait(GetLinkClockRate());
        }
        else
        {
            Log_DevPrintf("Receiving serial data.");
        }
    }
}

void System::LinkRecv()
{
    ReadPacket *packet = ReadPacket::From(m_linkClientSocket);
    if (packet == nullptr)
    {
        Log_ErrorPrintf("Read error. Closing link connection.");
        closesocket(m_linkClientSocket);
        m_linkClientSocket = -1;
        return;
    }

    switch (packet->GetPacketCommand())
    {
    case COMMAND_CLOCKRATE:
        {
            m_linkExternalClockRate = packet->ReadUInt32();
            Log_DevPrintf("Link other-side clock rate: %u hz", m_linkExternalClockRate);
            break;
        }

    case COMMAND_DATA:
        {
            byte data = packet->ReadByte();
            Log_DevPrintf("Link recieved data: 0x%02X", data);
            if ((m_serial_control & (1 << 0)))
            {
                if ((m_serial_control & (1 << 7)))
                {
                    m_serial_data = data;
                    LinkWait(m_linkExternalClockRate);
                }
                else
                {
                    Log_WarningPrintf("Serial data recieved with transfer flag unset, dropping");
                }
            }
            else
            {
                Log_WarningPrintf("Serial data received with internal clock set, dropping");
            }
            
            break;
        }
    }

    delete packet;
}

void System::LinkSend(uint8 value)
{
    // wait clocks upfront, in case send fails
    if ((m_serial_control & (1 << 0)))
        LinkWait(GetLinkClockRate());
    else
        LinkWait(m_linkExternalClockRate);

    // send packet
    WritePacket packet(COMMAND_DATA);
    packet << value;
    if (!packet.Send(m_linkClientSocket))
    {
        Log_ErrorPrintf("Write error. Closing link connection.");
        closesocket(m_linkClientSocket);
        m_linkClientSocket = -1;
    }
}
