#include "serial.h"
#include "system.h"
#include "link.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Log.h"
Log_SetChannel(Serial);

Serial::Serial(System *system)
    : m_system(system)
    , m_serial_control(0x00)
    , m_serial_read_data(0xFF)
    , m_serial_write_data(0xFF)
    , m_serial_wait_clocks(0)
    , m_sequence(0)
    , m_clocks_since_transfer_start(0)
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
            // Do we have a client?
            AutoReleasePtr<LinkSocket> socket = LinkConnectionManager::GetInstance().GetClientSocket();
            if (socket != nullptr)
            {
                Log_DevPrintf("Serial send clock %u and data 0x%02X", GetTransferClocks(), m_serial_write_data);

                // Send the byte to the client.
                WritePacket packet(LINK_COMMAND_CLOCK_DATA);
                packet << uint32(++m_sequence) << uint32(GetTransferClocks()) << uint8(m_serial_write_data);
                if (socket->SendPacket(&packet))
                {
                    // Wait for ACK (i.e. DATA) before simulating.
                    return;
                }
            }

            // No client, or a send error. so just "clock out" nothing
            m_serial_read_data = 0xFF;
            m_serial_wait_clocks = GetTransferClocks();
        }
    }
    else
    {
        // Transfer in progress?
        if (m_serial_wait_clocks > 0 || m_clocks_since_transfer_start > 0)
        {
            Log_DevPrintf("Cancelling wait for serial data.");
            m_serial_wait_clocks = 0;
            m_clocks_since_transfer_start = 0;
        }
    }
}

void Serial::SetSerialData(uint8 value)
{
    m_serial_write_data = value;
}

void Serial::Reset()
{
    m_serial_control = 0x00;
    m_serial_read_data = 0xFF;
    m_serial_write_data = 0x00;
    m_sequence = 0;
    m_serial_wait_clocks = 0;
    m_clocks_since_transfer_start = 0;
}

void Serial::EndTransfer(uint32 clocks)
{
    if (m_clocks_since_transfer_start >= clocks)
    {
        Log_DevPrintf("Late-firing serial interrupt.");

        m_serial_control &= ~(1 << 7);
        m_serial_wait_clocks = 0;
        m_clocks_since_transfer_start = 0;
        m_system->CPUInterruptRequest(CPU_INT_SERIAL);
    }
    else
    {
        m_serial_wait_clocks = clocks - m_clocks_since_transfer_start;
    }
}

void Serial::ExecuteFor(uint32 clocks)
{
    // counter
    if (m_serial_control & (1 << 7))
        m_clocks_since_transfer_start += clocks;

    // transfers
    if (m_serial_wait_clocks > 0)
    {
        if (clocks >= m_serial_wait_clocks)
        {
            Log_DevPrintf("Firing serial interrupt.");
            m_serial_control &= ~(1 << 7);
            m_serial_wait_clocks = 0;
            m_clocks_since_transfer_start = 0;
        }
        else
        {
            m_serial_wait_clocks -= clocks;
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
        m_serial_read_data = 0xFF;
        m_serial_write_data = 0x00;
        m_sequence = 0;
        m_serial_wait_clocks = 0;
        m_clocks_since_transfer_start = 0;
        return;
    }

    // read packets
    ReadPacket *packet;
    while ((packet = socket->GetPacket()) != nullptr)
    {
        switch (packet->GetPacketCommand())
        {
        case LINK_COMMAND_CLOCK_DATA:
            {
                uint32 sequence = packet->ReadUInt32();
                uint32 clocks = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();

                // Has our transfer been activated as well? (and with an external clock)
                if ((m_serial_control & 0x81) == 0x80)
                {
                    Log_DevPrintf("Received sequence (%u) clock (%u) and data (0x%02X), sending response (0x%02X)", sequence, clocks, data, m_serial_write_data);

                    // Send the response back immediately.
                    WritePacket response(LINK_COMMAND_DATA);
                    response << uint32(sequence) << uint32(clocks) << uint8(m_serial_write_data);
                    socket->SendPacket(&response);

                    // Queue interrupt fire
                    m_serial_read_data = data;
                    EndTransfer(clocks);
                }
                else
                {
                    Log_DevPrintf("Received sequence (%u) clock (%u) and data (0x%02X), sending UNEXPECTED response", sequence, clocks, data);

                    // Send the response back immediately.
                    WritePacket response(LINK_COMMAND_DATA);
                    response << uint32(sequence) << uint32(clocks) << uint8(0xFF);
                    socket->SendPacket(&response);
                }

                break;
            }

        case LINK_COMMAND_DATA:
            {
                // Got a response sent back to us for our clock
                uint32 sequence = packet->ReadUInt32();
                uint32 clocks = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();

                // Correct sequence number?
                if (m_sequence == sequence && (m_serial_control & 0x81) == 0x81)
                {
                    Log_DevPrintf("Received serial data response 0x%02X", data);

                    // Queue interrupt fire
                    m_serial_read_data = data;
                    EndTransfer(clocks);
                }
                else if (m_sequence == sequence)
                {
                    Log_WarningPrintf("Received serial data response 0x%02X after transfer cancelled, dropping.", data);
                }
                else
                {
                    Log_WarningPrintf("Received serial data response 0x%02X with incorrect sequence, dropping.", data);
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

