#include "YBaseLib/BinaryReadBuffer.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryWriteBuffer.h"
#include "YBaseLib/BinaryWriter.h"
#include "YBaseLib/Common.h"
#include "YBaseLib/Singleton.h"
#include "YBaseLib/Sockets/BufferedStreamSocket.h"
#include "YBaseLib/Sockets/SocketMultiplexer.h"
#include "YBaseLib/Timer.h"

class System;

enum LINK_COMMAND
{
  LINK_COMMAND_HELLO,
  LINK_COMMAND_CLOCK,
  LINK_COMMAND_DATA,
  LINK_COMMAND_NOT_READY,
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

  ReadPacket* GetPacket();

  bool SendPacket(LINK_COMMAND command, const void* pData, size_t dataLength);
  bool SendPacket(WritePacket* pPacket);

protected:
  virtual void OnConnected();
  virtual void OnDisconnected(Error* pError);
  virtual void OnRead();

  bool m_active;
};

class LinkConnectionManager : public Singleton<LinkConnectionManager>
{
  friend LinkSocket;

public:
  enum LinkState
  {
    LinkState_NotConnected,
    LinkState_Connected,
    LinkState_Disconnected
  };

public:
  LinkConnectionManager();
  ~LinkConnectionManager();

  bool Host(const char* address, uint32 port, Error* pError);
  bool Connect(const char* address, uint32 port, Error* pError);

  bool SetClientSocket(LinkSocket* socket);
  void Shutdown();

  // Queues a packet for later pickup by main thread.
  void QueuePacket(ReadPacket* packet);

  // Send a packet from the main thread.
  void SendPacket(WritePacket* packet);

  // Pull data from network thread to main thread.
  LinkState MainThreadPull(ReadPacket** out_packet);

private:
  bool CreateMultiplexer(Error* pError);

  SocketMultiplexer* m_multiplexer;
  ListenSocket* m_listen_socket;
  LinkSocket* m_client_socket;

  PODArray<ReadPacket*> m_packet_queue;
  LinkState m_state;
  Mutex m_lock;
};
