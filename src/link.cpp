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
    , m_active(false)
{

}

LinkSocket::~LinkSocket()
{
    DebugAssert(!m_active);
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
    // Send hello packet.
    WritePacket packet(LINK_COMMAND_HELLO);
    packet << uint32(NW_VERSION);
    SendPacket(&packet);
}

void LinkSocket::OnDisconnected(Error *pError)
{
    Log_ErrorPrintf("Link socket disconnected: %s", pError->GetErrorCodeAndDescription().GetCharArray());
    if (m_active)
    {
        LinkConnectionManager::GetInstance().SetClientSocket(nullptr);
        m_active = false;
    }
}

void LinkSocket::OnRead()
{
    // Read a packet in.
    ReadPacket *packet = GetPacket();
    if (packet == nullptr)
        return;

    // Examine the header for packets we handle internally.
    switch (packet->GetPacketCommand())
    {
    case LINK_COMMAND_HELLO:
        {
            uint32 version = packet->ReadUInt32();
            Log_DevPrintf("Link socket received hello: version %u", version);
            if (version != NW_VERSION)
            {
                Log_ErrorPrintf("Network version mismatch (client: %u, us: %u)", version, NW_VERSION);
                Close();
                return;
            }

            // Do we already have a connection?
            m_active = LinkConnectionManager::GetInstance().SetClientSocket(this);
            if (!m_active)
            {
                Log_ErrorPrintf("Rejecting client connection, already have a client.");
                Close();
                return;
            }

            // Cleanup memory since we're not queueing this packet.
            delete packet;
            break;
        }

    default:
        {
            // let main thread handle it
            LinkConnectionManager::GetInstance().QueuePacket(packet);
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
    Shutdown();
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

    // Connection in progress.
    return true;
}

bool LinkConnectionManager::SetClientSocket(LinkSocket *socket)
{
    m_lock.Lock();

    // Is this a new connection?
    if (socket != nullptr)
    {
        // Already has a connection?
        if (m_client_socket != nullptr)
        {
            m_lock.Unlock();
            return false;
        }

        // State change.
        DebugAssert(m_state != LinkState_Connected);
        m_state = LinkState_Connected;
        m_client_socket = socket;
        m_client_socket->AddRef();
    }
    else
    {
        // Disconnecting?
        if (m_client_socket != nullptr)
        {
            // State change.
            DebugAssert(m_state == LinkState_Connected);
            m_state = LinkState_Disconnected;

            // Remove all packets from the queue.
            while (m_packet_queue.GetSize() > 0)
                delete m_packet_queue.PopBack();

            // Release reference.
            m_client_socket->Release();
            m_client_socket = nullptr;
        }
    }

    m_lock.Unlock();
    return true;
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
    // Stop network thread first.
    if (m_multiplexer != nullptr)
        m_multiplexer->StopWorkerThreads();

    // Close sockets.
    if (m_listen_socket != nullptr)
    {
        m_listen_socket->Close();
        m_listen_socket->Release();
        m_listen_socket = nullptr;
    }
    if (m_client_socket != nullptr)
    {
        // Calling Close() will cause the pointer to be set to null
        m_client_socket->Close();
        DebugAssert(m_client_socket == nullptr);
    }

    // Cleanup multiplexer.
    delete m_multiplexer;
    m_multiplexer = nullptr;
}

void LinkConnectionManager::QueuePacket(ReadPacket *packet)
{
    m_lock.Lock();
    m_packet_queue.Add(packet);
    m_lock.Unlock();
}

void LinkConnectionManager::SendPacket(WritePacket *packet)
{
    // This mess is necessary because of the locking order (read below)
    LinkSocket *socket;
    m_lock.Lock();
    if ((socket = m_client_socket) != nullptr)
        socket->AddRef();
    m_lock.Unlock();

    if (socket != nullptr)
        socket->SendPacket(packet);
}

LinkConnectionManager::LinkState LinkConnectionManager::MainThreadPull(ReadPacket **out_packet)
{
    m_lock.Lock();

    // Pull state.
    LinkState state = m_state;
    if (state != LinkState_Connected)
    {
        // If set to disconnected state, reset after returning it once.
        if (state == LinkState_Disconnected)
            m_state = LinkState_NotConnected;

        m_lock.Unlock();
        return state;
    }

    // Pull a packet whilst locked.
    // We can only do one at a time since if a packet is sent in response, that will result in
    // reversed locking order compared to receiving, which will deadlock.
    if (m_packet_queue.GetSize() > 0)
        *out_packet = m_packet_queue.PopFront();
    else
        *out_packet = nullptr;

    // Return state
    m_lock.Unlock();
    return state;
}
