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
    COMMAND_CLOCK_AND_DATA,
    COMMAND_DATA,
    COMMAND_DATA_ACK,
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
    m_linkWaitClocks = 0;
    m_linkSocketPollClocks = 0;
    m_linkExternalClockRate = 8192;
    m_linkBufferedReadData = 0xFF;
    m_linkHasBufferedReadData = false;
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
    m_linkBufferedReadData = 0xFF;
    m_linkHasBufferedReadData = false;
}

bool System::LinkHost(uint32 port, Error *pError)
{
    // already connected?
    if (m_linkClientSocket >= 0)
    {
        pError->SetErrorUser(1, "Can't host when connected as client.");
        return false;
    }

    // initialize winsock
    if (!InitializeWinsock())
    {
        pError->SetErrorUser(1, "Winsock initialization failed");
        return false;
    }

    // set up bind address
    sockaddr_in listenaddr;
    Y_memzero(&listenaddr, sizeof(listenaddr));
    listenaddr.sin_family = AF_INET;
    listenaddr.sin_addr.s_addr = INADDR_ANY;
    listenaddr.sin_port = htons((uint16)port);
    
    // create socket
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
        pError->SetErrorSocket(WSAGetLastError());
        return false;
    }

    // bind to listen address
    if (bind(fd, (const sockaddr *)&listenaddr, sizeof(listenaddr)) != 0)
    {
        pError->SetErrorSocket(WSAGetLastError());
        closesocket(fd);
        return false;
    }

    // accept connections
    if (listen(fd, 1) != 0)
    {
        pError->SetErrorSocket(WSAGetLastError());
        closesocket(fd);
        return false;
    }

    // done
    Log_InfoPrintf("Link hosting.");
    m_linkListenSocket = fd;
    return true;
}

bool System::LinkConnect(const char *host, uint32 port, Error *pError)
{
    // listening?
    if (m_linkListenSocket >= 0)
    {
        pError->SetErrorUser(1, "Can't connect when hosting.");
        return false;
    }

    // initialize winsock
    if (!InitializeWinsock())
    {
        pError->SetErrorUser(1, "Winsock initialization failed");
        return false;
    }

    // set up target
    sockaddr_in sa;
    Y_memzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1)
    {
        Log_ErrorPrintf("inet_pton() failed: %d", WSAGetLastError());
        return false;
    }
    
    // create socket
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
        Log_ErrorPrintf("socket() failed: %d", WSAGetLastError());
        return false;
    }

    // connect to server
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

    // ready to go
    m_linkClientSocket = client_fd;
    Log_InfoPrintf("Link connection established.");
}

void System::LinkTick(uint32 clocks)
{
    // handle waits
    if (m_linkWaitClocks > 0)
    {
        if (clocks >= m_linkWaitClocks)
        {
            m_linkWaitClocks = 0;
            m_serial_control &= ~(1 << 7);
            if (m_linkHasBufferedReadData)
            {
                m_serial_data = m_linkBufferedReadData;
                m_linkHasBufferedReadData = false;
            }

            Log_DevPrintf("Serial interrupt fired.");
            CPUInterruptRequest(CPU_INT_SERIAL);
        }
        else
        {
            m_linkWaitClocks -= clocks;
        }
    }

    // can we poll?
    if (m_linkListenSocket >= 0 || m_linkClientSocket >= 0)
    {
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
    //uint32 waitclocks = 4194304 / clockrate;
    uint32 waitclocks;
    if (clockrate == 262144)
        waitclocks = 128;
    else
        waitclocks = 4096;

    m_linkWaitClocks = waitclocks;
}

void System::LinkSetControl(uint8 value)
{
    Log_DevPrintf("Serial control set to 0x%02X", value);
    m_serial_control = value;

    // start transfer?
    if (value & (1 << 7))
        LinkWrite();
}

void System::LinkClose()
{
    if (m_linkClientSocket >= 0)
    {
        closesocket(m_linkClientSocket);
        m_linkClientSocket = -1;
    }

    m_linkExternalClockRate = 8192;
    m_linkBufferedReadData = 0xFF;
    m_linkHasBufferedReadData = false;
}

void System::LinkRecv()
{
    ReadPacket *packet = ReadPacket::From(m_linkClientSocket);
    if (packet == nullptr)
    {
        Log_ErrorPrintf("Read error. Closing link connection.");
        LinkClose();
        return;
    }

    switch (packet->GetPacketCommand())
    {
    case COMMAND_CLOCK_AND_DATA:
        {
            uint32 external_clock_rate = packet->ReadUInt32();
            byte data = packet->ReadByte();
            Log_DevPrintf("Receieved clock(%u hz) + data (0x%02X)", external_clock_rate, data);

            // are we set to use an external clock?
            if (!(m_serial_control & (1 << 0)))
            {
                // store buffered byte
                m_linkHasBufferedReadData = true;
                m_linkExternalClockRate = external_clock_rate;
                m_linkBufferedReadData = data;

                // are we waiting for a transfer?
                if (m_serial_control & (1 << 7))
                {
                    Log_DevPrintf("Sending serial data (unbuffered): 0x%02X (external clock %u hz)", m_serial_data, m_linkExternalClockRate);

                    // send our data immediately
                    WritePacket send_packet(COMMAND_DATA);
                    send_packet << byte(m_serial_data);
                    if (!send_packet.Send(m_linkClientSocket))
                    {
                        Log_ErrorPrintf("Write error. Closing link connection.");
                        LinkClose();
                    }
                    
                    // queue move from buffer->register
                    LinkWait(external_clock_rate);
                }
            }
            else
            {
                Log_WarningPrintf("Clock+data recieved while using internal clock. Dropping packet.");
            }

            break;
        }

    case COMMAND_DATA:
        {
            byte data = packet->ReadByte();
            Log_DevPrintf("Link recieved data (0x%02X)", data);

            // are we set to use an internal clock? (i.e. this is a response)
            if (m_serial_control & (1 << 0))
            {
                // are we waiting for a transfer?
                if (m_serial_control & (1 << 7))
                {
                    // send the acknowledgment
                    WritePacket ack(COMMAND_DATA_ACK);
                    if (!ack.Send(m_linkClientSocket))
                        LinkClose();

                    // buffer the read, and flag it when done
                    m_linkHasBufferedReadData = true;
                    m_linkBufferedReadData = data;
                    LinkWait(GetLinkClockRate());
                }
                else
                {
                    // we received a packet without actually clocking one out ourselves
                    Log_DevPrintf("Data recieved without clocking. Queueing packet.");
                    m_linkBufferedReadData = true;
                    m_linkBufferedReadData = true;
                }
            }
            else
            {
                Log_WarningPrintf("Serial data (without clock) received with external clock set, dropping");
            }
            
            break;
        }

    case COMMAND_DATA_ACK:
        {
            // ok from the server to push through the pending data
            LinkWait(m_linkExternalClockRate);
            break;
        }
    }

    delete packet;
}

void System::LinkWrite()
{
    // are we providing the clock?
    if (m_serial_control & (1 << 0))
    {
        Log_DevPrintf("Sending serial data: 0x%02X (internal clock %u hz)", m_serial_data, GetLinkClockRate());

        // do we have a connection?
        if (m_linkClientSocket >= 0)
        {
            // send packet away
            WritePacket packet(COMMAND_CLOCK_AND_DATA);
            packet << uint32(GetLinkClockRate());
            packet << byte(m_serial_data);
            if (packet.Send(m_linkClientSocket))
            {
                // did the client already send to us (timing off, but only slightly)
                if (m_linkHasBufferedReadData)
                {
                    // send the acknowledgment
                    WritePacket ack(COMMAND_DATA_ACK);
                    if (!packet.Send(m_linkClientSocket))
                        LinkClose();

                    // immediately queue this
                    LinkWait(GetLinkClockRate());
                    return;
                }
                else
                {
                    // send success, stay with transfer flag on until we get a response.
                    return;
                }
            }
            else
            {
                // send failed
                Log_ErrorPrintf("Write error. Closing link connection.");
                LinkClose();
            }
        }

        // if we're here, it means the connection dropped or we didn't have one, so just complete the transfer
        m_linkBufferedReadData = 0xFF;
        m_linkHasBufferedReadData = true;
        LinkWait(GetLinkClockRate());
    }
    else
    {
        Log_DevPrintf("Receiving serial data from external clock.");

        // have we actually got a link connection?
        // if not, just leave the game to time out
        if (m_linkClientSocket >= 0)
        {
            // do we have any data pending?
            // if not, means the external clock (the server) hasn't sent their data, so wait
            if (m_linkHasBufferedReadData)
            {
                Log_DevPrintf("Sending serial data (buffered): 0x%02X (external clock %u hz)", m_serial_data, m_linkExternalClockRate);

                // send our data across
                WritePacket packet(COMMAND_DATA);
                packet << byte(m_serial_data);
                if (!packet.Send(m_linkClientSocket))
                {
                    // send failed
                    Log_ErrorPrintf("Write error. Closing link connection.");
                    LinkClose();
                }

                // read the buffered data after clock cycles
                //LinkWait(m_linkExternalClockRate);
                // wait for ack before passing data through
            }
        }
    }
}
