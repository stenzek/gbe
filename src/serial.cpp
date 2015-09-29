#include "serial.h"
#include "system.h"
#include "link.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Log.h"
Log_SetChannel(Serial);

Serial::Serial(System *system)
    : m_system(system)
    , m_serial_control(0x00)
    , m_serial_data(0xFF)
    , m_serial_send_buffer(0x00)
    , m_serial_receive_buffer(0x00)
    , m_serial_has_send_buffer(false)
    , m_serial_has_receive_buffer(false)
    , m_serial_pending_data(0xFF)
    , m_serial_pending_data_clocks(0)
    , m_serial_pending_ack(false)
{

}

Serial::~Serial()
{

}

uint32 Serial::GetTransferClocks() const
{
    bool fast_clock_rate = !!(m_serial_control & (1 << 1));
    if (fast_clock_rate)
        return 128;
    else
        return 4096;
}

void Serial::SetSerialControl(uint8 value)
{
    bool start_transfer = !!(value & (1 << 7));
    bool internal_clock = !!(value & (1 << 0));
    m_serial_control = value;

    // Start transfer?
    if (start_transfer)
    {
        // Are we the one providing the clock?
        m_clocks_since_transfer_start = 0;
        if (internal_clock)
        {
            // Fix these asserts at some point..
            //DebugAssert(!m_serial_pending_ack && m_serial_pending_data_clocks == 0);
            //m_serial_pending_ack = false;
            //m_serial_pending_data_clocks = 0;

            // Do we have a client?
            AutoReleasePtr<LinkSocket> socket = LinkConnectionManager::GetInstance().GetClientSocket();
            if (socket != nullptr)
            {
                Log_DevPrintf("Serial send clock %u and data 0x%02X", GetTransferClocks(), m_serial_data);

                // Send the byte to the client.
                WritePacket packet(LINK_COMMAND_CLOCK_AND_DATA);
                packet << uint32(GetTransferClocks()) << uint8(m_serial_data);
                if (socket->SendPacket(&packet))
                {
                    // Wait for ACK (i.e. DATA) before simulating.
                    m_serial_pending_ack = true;
                    return;
                }
            }

            // No client, or a send error. so just "clock out" nothing
            m_serial_pending_data = 0xFF;
            m_serial_pending_data_clocks = GetTransferClocks();
            m_serial_pending_ack = false;
        }
        else
        {
            // If we have no connection here, simply wait forever with no data.
            AutoReleasePtr<LinkSocket> socket = LinkConnectionManager::GetInstance().GetClientSocket();
            if (socket != nullptr)
            {
                // Have we had a byte clocked to us? 
                if (m_serial_has_receive_buffer)
                {
                    Log_DevPrintf("Serial send data 0x%02X", m_serial_data);

                    // Send the response back immediately.
                    WritePacket packet(LINK_COMMAND_DATA);
                    packet << uint32(m_serial_receive_clock) << uint8(m_serial_data);
                    socket->SendPacket(&packet);

                    // Move received byte to pending.
                    m_serial_pending_data = m_serial_receive_buffer;
                    m_serial_pending_data_clocks = m_serial_receive_clock;
                    m_serial_pending_ack = true;
                    m_serial_has_send_buffer = false;
                }
                else
                {
                    // The byte will be saved for later.
                    Log_DevPrintf("Buffered externally clocked data 0x%02X", m_serial_data);
                    m_serial_send_buffer = m_serial_data;
                    m_serial_has_send_buffer = true;
                }
            }
        }
    }
    else
    {
        // Transfer in progress?
        if (m_serial_pending_ack || m_serial_pending_data_clocks > 0 || m_serial_has_send_buffer)
        {
            Log_DevPrintf("Cancelling wait for serial data.");
            m_serial_pending_data = 0xFF;
            m_serial_pending_data_clocks = 0;
            m_serial_has_send_buffer = false;
            m_serial_pending_ack = false;
        }
    }
}

void Serial::SetSerialData(uint8 value)
{
    m_serial_data = value;
}

void Serial::Reset()
{
    m_serial_control = 0x00;
    m_serial_data = 0xFF;
    m_serial_pending_data = 0xFF;
    m_serial_pending_data_clocks = 0;
    m_serial_pending_ack = false;
}

void Serial::ExecuteFor(uint32 clocks)
{
    // counter
    if (m_serial_control & (1 << 7))
        m_clocks_since_transfer_start += clocks;

    // transfers
    if (!m_serial_pending_ack && m_serial_pending_data_clocks > 0)
    {
        if (clocks >= m_serial_pending_data_clocks)
        {
            m_serial_pending_data_clocks = 0;
            m_serial_control &= ~(1 << 7);
            m_serial_data = m_serial_pending_data;
            m_serial_pending_data = 0xFF;

            Log_DevPrintf("Firing serial interrupt.");
            m_system->CPUInterruptRequest(CPU_INT_SERIAL);
        }
        else
        {
            m_serial_pending_data_clocks -= clocks;
        }
    }

    // link socket activity
    // really need a packet queue here..
    AutoReleasePtr<LinkSocket> socket = LinkConnectionManager::GetInstance().GetClientSocket();
    if (socket != nullptr && socket->HasActivity())
        PollSocket(socket);
}

void Serial::PollSocket(LinkSocket *socket)
{
    // handle disconnections
    if (!socket->IsConnected())
    {
        m_serial_data = 0xFF;
        m_serial_pending_data = 0xFF;
        m_serial_pending_data_clocks = 0;
        m_serial_pending_ack = false;
        return;
    }

    // read packets
    ReadPacket *packet;
    while ((packet = socket->GetPacket()) != nullptr)
    {
        switch (packet->GetPacketCommand())
        {
        case LINK_COMMAND_CLOCK_AND_DATA:
            {
                uint32 clocks = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();

                // Have we sent with an external clock?
                if (m_serial_has_send_buffer)
                {
                    // Send the buffered byte
                    Log_DevPrintf("Received clock (%u) and data (0x%02X), sending buffered response", clocks, data, m_serial_send_buffer);

                    // Send the response back immediately.
                    WritePacket response(LINK_COMMAND_DATA);
                    response << uint32(clocks) << uint8(m_serial_send_buffer);
                    socket->SendPacket(&response);

                    // Move received byte to pending.
                    m_serial_pending_data = data;
                    m_serial_pending_data_clocks = clocks;
                    m_serial_pending_ack = true;
                    m_serial_has_send_buffer = false;
                    m_serial_has_receive_buffer = false;
                }
                else
                {
                    // Move to receive buffer.
                    Log_DevPrintf("Received clock (%u) and data (0x%02X), buffering", clocks, data);
                    m_serial_receive_buffer = data;
                    m_serial_receive_clock = clocks;
                    m_serial_has_receive_buffer = true;
                }

                break;
            }

        case LINK_COMMAND_DATA:
            {
                // Got a response sent back to us
                uint32 clocks = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();

                // Are we actually pending data?
                bool expectedAck = m_serial_pending_ack;
                if (m_serial_pending_ack)
                {
                    // Move to pending.
                    if (m_clocks_since_transfer_start < clocks)
                        m_serial_pending_data_clocks = clocks - m_clocks_since_transfer_start;
                    else
                        m_serial_pending_data_clocks = 1;

                    m_serial_pending_data = data;
                    m_serial_pending_ack = false;
                }

                // Send ACK
                WritePacket response(LINK_COMMAND_DATA_ACK);
                response << bool(expectedAck);
                socket->SendPacket(&response);

                break;
            }

        case LINK_COMMAND_DATA_ACK:
            {
                // Got an ACK from our externally clocked data
                bool expectedAck = packet->ReadBool();
                if (m_serial_pending_ack)
                {
                    if (expectedAck)
                    {
                        if (m_clocks_since_transfer_start < m_serial_pending_data_clocks)
                            m_serial_pending_data_clocks = m_serial_pending_data_clocks - m_clocks_since_transfer_start;
                        else
                            m_serial_pending_data_clocks = 1;
                    }
                    else
                    {
                        // Got an ack, but the other side didn't actually clock data out
                        m_serial_pending_data = 0xFF;
                        m_serial_pending_data_clocks = 0;
                        m_serial_data = 0xFF;
                    }

                    m_serial_pending_ack = false;
                }

                break;
            }

        default:
            {
                Log_ErrorPrintf("Received unknown command: %u", packet->GetPacketCommand());
                break;
            }
        }

        delete packet;
    }        
}

