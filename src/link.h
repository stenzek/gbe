#include "YBaseLib/Common.h"
#include "YBaseLib/Sockets/BufferedStreamSocket.h"
#include "YBaseLib/Sockets/SocketMultiplexer.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryReadBuffer.h"
#include "YBaseLib/BinaryWriter.h"
#include "YBaseLib/BinaryWriteBuffer.h"
#include "YBaseLib/Singleton.h"

class System;

enum LINK_COMMAND
{
    LINK_COMMAND_HELLO,
    LINK_COMMAND_CLOCK_AND_DATA,
    LINK_COMMAND_
    LINK_COMMAND_DATA,
    LINK_COMMAND_DATA_ACK,
};

struct LINK_PACKET_HEADER
{
    uint8 command;
    uint16 length;
};

// TODO: "Duplex buffer"

class ReadPacket : public BinaryReadBuffer
{
public:
    ReadPacket(LINK_COMMAND command, uint16 size);
    virtual ~ReadPacket();

    const LINK_COMMAND GetPacketCommand() const { return m_command; }
    const size_t GetPacketSize() const { return (size_t)m_packetSize; }

private:
    LINK_COMMAND m_command;
    uint16 m_packetSize;
};

class WritePacket : public BinaryWriteBuffer
{
public:
    WritePacket(LINK_COMMAND command);

    virtual ~WritePacket();

    const LINK_COMMAND GetPacketCommand() const { return m_command; }
    const size_t GetPacketSize() const { return (size_t)m_pStream->GetSize(); }

private:
    LINK_COMMAND m_command;
};

class LinkSocket : public BufferedStreamSocket 
{
public:
    LinkSocket();
    virtual ~LinkSocket();

    const bool HasActivity() const { return m_hasActivity; }

    ReadPacket *GetPacket();

    bool SendPacket(LINK_COMMAND command, const void *pData, size_t dataLength);
    bool SendPacket(WritePacket *pPacket);

protected:
    virtual void OnConnected();
    virtual void OnDisconnected(Error *pError);
    virtual void OnRead();

    volatile bool m_hasActivity;
};

class LinkConnectionManager : public Singleton<LinkConnectionManager>
{
    friend LinkSocket;

public:
    LinkConnectionManager();
    ~LinkConnectionManager();

    LinkSocket *GetClientSocket() const;
    System *GetSystem() const;

    bool Host(const char *address, uint32 port, Error *pError);
    bool Connect(const char *address, uint32 port, Error *pError);

    void SetSystem(System *system);
    void SetClientSocket(LinkSocket *socket);
    void Shutdown();

private:
    bool CreateMultiplexer(Error *pError);

    SocketMultiplexer *m_multiplexer;
    ListenSocket *m_listen_socket;
    LinkSocket *m_client_socket;
    System *m_system;
};

