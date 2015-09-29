#include "link.h"
#include "system.h"
#include "serial.h"
#include "YBaseLib/Sockets/ListenSocket.h"
#include "YBaseLib/Sockets/SocketMultiplexer.h"
#include "YBaseLib/Sockets/SocketAddress.h"
#include "YBaseLib/Log.h"
Log_SetChannel(Link);

static const uint32 NW_VERSION = 2;

ReadPacket::ReadPacket(LINK_COMMAND command, uint16 size) 
    : BinaryReadBuffer(Max(size, (uint16)1))
    , m_command(command)
    , m_packetSize(size)
{

}

ReadPacket::~ReadPacket()
{

}

WritePacket::WritePacket(LINK_COMMAND command) 
    : BinaryWriteBuffer()
    , m_command(command)
{

}

WritePacket::~WritePacket()
{

}

LinkSocket::LinkSocket() 
    : BufferedStreamSocket()
    , m_hasActivity(false)
{

}

LinkSocket::~LinkSocket()
{

}

ReadPacket *LinkSocket::GetPacket()
{
    if (!IsConnected())
        return nullptr;

    const void *pBuffer;
    size_t bytesAvailable;
    if (!AcquireReadBuffer(&pBuffer, &bytesAvailable))
        return nullptr;

    const LINK_PACKET_HEADER *pPacketHeader = (const LINK_PACKET_HEADER *)pBuffer;
    if (bytesAvailable < sizeof(LINK_PACKET_HEADER) || bytesAvailable < ((size_t)pPacketHeader->length + sizeof(LINK_PACKET_HEADER)))
    {
        ReleaseReadBuffer(0);
        return nullptr;
    }

    ReadPacket *pPacket = new ReadPacket((LINK_COMMAND)pPacketHeader->command, pPacketHeader->length);
    Y_memcpy(pPacket->GetBufferPointer(), (const byte *)pBuffer + sizeof(LINK_PACKET_HEADER), pPacketHeader->length);
    ReleaseReadBuffer((size_t)pPacketHeader->length + sizeof(LINK_PACKET_HEADER));
    return pPacket;
}

bool LinkSocket::SendPacket(WritePacket *pPacket)
{
    return SendPacket(pPacket->GetPacketCommand(), pPacket->GetBufferPointer(), (size_t)pPacket->GetStream()->GetSize());
}

bool LinkSocket::SendPacket(LINK_COMMAND command, const void *pData, size_t dataLength)
{
    LINK_PACKET_HEADER header;
    header.command = (uint8)command;
    header.length = (uint8)dataLength;

    if (header.length > 0)
    {
        const void *pBuffers[2] = { &header, pData };
        size_t bufferLengths[2] = { sizeof(header), dataLength };
        return (WriteVector(pBuffers, bufferLengths, countof(pBuffers)) == dataLength + sizeof(header));
    }
    else
    {
        // no payload
        return (Write(&header, sizeof(header)) == sizeof(header));
    }
}

void LinkSocket::OnConnected()
{
    // Do we already have a connection?
    if (LinkConnectionManager::GetInstance().GetClientSocket() != nullptr)
    {
        // Close this connection
        Close();
        return;
    }

    // Set client socket
    LinkConnectionManager::GetInstance().SetClientSocket(this);

    // Send hello packet
    WritePacket packet(LINK_COMMAND_HELLO);
    packet << uint32(NW_VERSION);
    SendPacket(&packet);
}

void LinkSocket::OnDisconnected(Error *pError)
{
    if (LinkConnectionManager::GetInstance().GetClientSocket() == this)
        LinkConnectionManager::GetInstance().SetClientSocket(nullptr);

    Log_ErrorPrintf("Link socket disconnected: %s", pError->GetErrorCodeAndDescription().GetCharArray());
    m_hasActivity = true;
}

void LinkSocket::OnRead()
{
    // Get the read buffer size.
    const void *pBuffer;
    size_t bytesAvailable;
    if (!AcquireReadBuffer(&pBuffer, &bytesAvailable))
        return;

    // Check we have the entire packet.
    const LINK_PACKET_HEADER *pPacketHeader = (const LINK_PACKET_HEADER *)pBuffer;
    if (bytesAvailable < ((size_t)pPacketHeader->length + sizeof(LINK_PACKET_HEADER)))
    {
        ReleaseReadBuffer(0);
        return;
    }

    // Get the command index.
    LINK_COMMAND command = (LINK_COMMAND)pPacketHeader->command;
    ReleaseReadBuffer(0);

    // Examine the header for packets we handle internally.
    switch (command)
    {
    case LINK_COMMAND_HELLO:
        {
            ReadPacket *pPacket = GetPacket();
            if (pPacket != nullptr)
            {
                uint32 version = pPacket->ReadUInt32();
                Log_DevPrintf("Link socket received hello: version %u", version);
                if (version != NW_VERSION)
                {
                    Log_ErrorPrintf("Network version mismatch (client: %u, us: %u)", version, NW_VERSION);
                    Close();
                    return;
                }
            }

            break;
        }

    default:
        {
            // let main thread handle it
            m_hasActivity = true;
            break;
        }
    }
}

LinkConnectionManager::LinkConnectionManager()
    : m_multiplexer(nullptr)
    , m_listen_socket(nullptr)
{
    
}

LinkConnectionManager::~LinkConnectionManager()
{
    // delete multiplexer on shutdown
    SAFE_RELEASE(m_listen_socket);
    SAFE_RELEASE(m_client_socket);
    delete m_multiplexer;
}

LinkSocket *LinkConnectionManager::GetClientSocket() const
{
    LinkSocket *socket = m_client_socket;
    if (socket != nullptr)
        socket->AddRef();

    return socket;
}

System *LinkConnectionManager::GetSystem() const
{
    return m_system;
}

bool LinkConnectionManager::Host(const char *address, uint32 port, Error *pError)
{
    if (m_listen_socket != nullptr)
    {
        pError->SetErrorUser(1, "Already hosting.");
        return false;
    }

    if (m_client_socket != nullptr)
    {
        pError->SetErrorUser(1, "Can't host while connected.");
        return false;
    }

    if (m_multiplexer == nullptr && !CreateMultiplexer(pError))
        return false;

    SocketAddress bind;
    bind.Parse(SocketAddress::Type_IPv4, (address != nullptr) ? address : "0.0.0.0", port, &bind);
    m_listen_socket = m_multiplexer->CreateListenSocket<LinkSocket>(&bind, pError);
    return (m_listen_socket != nullptr);
}

bool LinkConnectionManager::Connect(const char *address, uint32 port, Error *pError)
{
    if (m_client_socket != nullptr)
    {
        pError->SetErrorUser(1, "Already connected.");
        return false;
    }

    if (m_listen_socket != nullptr)
    {
        pError->SetErrorUser(1, "Can't connect while hosting.");
        return false;
    }

    if (m_multiplexer == nullptr && !CreateMultiplexer(pError))
        return false;

    SocketAddress bind;
    bind.Parse(SocketAddress::Type_IPv4, address, port, &bind);
    if (m_multiplexer->ConnectStreamSocket<LinkSocket>(&bind, pError) == nullptr)
        return false;

    DebugAssert(m_client_socket != nullptr);
    return true;
}

void LinkConnectionManager::SetSystem(System *system)
{
    m_system = system;
}

void LinkConnectionManager::SetClientSocket(LinkSocket *socket)
{
    if (m_client_socket != nullptr)
        m_client_socket->Release();

    if ((m_client_socket = socket) != nullptr)
        m_client_socket->AddRef();
}

bool LinkConnectionManager::CreateMultiplexer(Error *pError)
{
    if (m_multiplexer == nullptr)
    {
        m_multiplexer = SocketMultiplexer::Create(pError);
        if (m_multiplexer == nullptr || !m_multiplexer->CreateWorkerThreads(1))
        {
            delete m_multiplexer;
            m_multiplexer = nullptr;
            return false;
        }
    }

    return true;
}

void LinkConnectionManager::Shutdown()
{
    if (m_listen_socket != nullptr)
    {
        m_listen_socket->Close();
        m_listen_socket->Release();
        m_listen_socket = nullptr;
    }

    // Calling Close() will cause the pointer to be set to null
    if (m_client_socket != nullptr)
    {
        m_client_socket->Close();
        DebugAssert(m_client_socket == nullptr);
    }

    if (m_multiplexer != nullptr)
    {
        m_multiplexer->CloseAll();
        m_multiplexer->StopWorkerThreads();
        delete m_multiplexer;
        m_multiplexer = nullptr;
    }
}
