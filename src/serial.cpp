#include "serial.h"
#include "system.h"
#include "link.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/NumericLimits.h"
Log_SetChannel(Serial);

Serial::Serial(System *system)
    : m_system(system)
    , m_serial_control(0x00)
    , m_serial_read_data(0xFF)
    , m_serial_write_data(0xFF)
    , m_serial_wait_clocks(0)
    , m_sequence(0)
    , m_expected_sequence(Y_UINT32_MAX)
    , m_external_clocks(0)
    , m_clocks_since_transfer_start(0)
    , m_clock_pending(false)
{
    // FIXME - randomize starting sequence
    m_sequence = (uint32)GetCurrentProcessId();
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
            // Forget about anything clocked to us
            m_clock_pending = false;

            // Do we have a client?
            AutoReleasePtr<LinkSocket> socket = LinkConnectionManager::GetInstance().GetClientSocket();
            if (socket != nullptr)
            {
                // Increment sequence number
                Log_DevPrintf("Serial send sequence %u and clock %u", m_sequence + 1, GetTransferClocks());
                m_sequence++;

                // Send the byte to the client.
                WritePacket packet(LINK_COMMAND_CLOCK);
                packet << uint32(m_sequence) << uint32(GetTransferClocks());
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
        else
        {
            Log_DevPrintf("Waiting for externally clocked data.");
//             // Has the other side clocked data out, and we're running late?
//             if (m_clock_pending)
//             {
//                 AutoReleasePtr<LinkSocket> socket = LinkConnectionManager::GetInstance().GetClientSocket();
//                 if (socket != nullptr)
//                 {
//                     // Send our response.
//                     WritePacket packet(LINK_COMMAND_DATA);
//                     packet << uint32(m_expected_sequence) << uint8(m_serial_write_data);
//                     socket->SendPacket(&packet);
//                 }
// 
//                 m_clock_pending = false;
//             }
        }
    }
//     else
//     {
//         // Transfer in progress?
//         if (m_serial_wait_clocks > 0 || m_clocks_since_transfer_start > 0)
//         {
//             Log_DevPrintf("Cancelling wait for serial data.");
//             m_serial_wait_clocks = 0;
//             m_clocks_since_transfer_start = 0;
//             m_clock_pending = false;
//         }
//     }
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
        case LINK_COMMAND_CLOCK:
            {
                uint32 sequence = packet->ReadUInt32();
                uint32 clocks = packet->ReadUInt32();
                m_clock_pending = false;

                // Has our transfer been activated as well? (and with an external clock)
                if ((m_serial_control & 0x81) == 0x80)
                {
                    Log_DevPrintf("Received sequence (%u) and clock (%u), sending response (0x%02X)", sequence, clocks, m_serial_write_data);

                    // Send the response back immediately.
                    WritePacket response(LINK_COMMAND_DATA);
                    response << uint32(sequence) << uint8(m_serial_write_data);
                    socket->SendPacket(&response);

                    // Set expected sequence.
                    m_expected_sequence = sequence;
                    m_external_clocks = clocks;
                }
                else
                {
                    Log_DevPrintf("Received sequence (%u) and clock (%u), waiting for a while in case we send data", sequence, clocks);
                    m_expected_sequence = sequence;
                    m_external_clocks = clocks;
                    m_clock_pending = true;

                    // Send the response back immediately.
                    //WritePacket response(LINK_COMMAND_DATA);
                    //response << uint32(sequence) << uint8(0xFF);
                    //socket->SendPacket(&response);
                }

                break;
            }

        case LINK_COMMAND_DATA:
            {
                // Got a response sent back to us for our clock
                uint32 sequence = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();
                m_clock_pending = false;

                // Is this a response with an external clock?
                if ((m_serial_control & 0x81) == 0x80)
                {
                    // Check sequence number.
                    if (sequence == m_expected_sequence)
                    {
                        Log_DevPrintf("Ending transfer sequence %u with externally clocked data 0x%02X", sequence, data);

                        // Fire the interrupt.
                        m_serial_read_data = data;
                        EndTransfer(m_external_clocks);
                    }
                    else
                    {
                        Log_WarningPrintf("Received externally-clocked serial data (0x%02X) with incorrect sequence, ignoring.", sequence, data);
                    }
                }
                else if ((m_serial_control & 0x81) == 0x81)
                {
                    // We have the internal clock. Check the sequence number to ensure this is a correct response.
                    if (sequence == m_sequence)
                    {
                        Log_DevPrintf("Ending transfer sequence %u with clocked data 0x%02X", data);

                        // Send our data.
                        WritePacket response(LINK_COMMAND_DATA);
                        response << uint32(sequence) << uint8(m_serial_write_data);
                        socket->SendPacket(&response);

                        // Set received data.
                        m_serial_read_data = data;
                        EndTransfer(GetTransferClocks());
                    }
                    else
                    {
                        Log_WarningPrintf("Received serial data (0x%02X) with incorrect sequence, ignoring.", data);
                    }
                }
                else
                {
                    Log_WarningPrintf("Received serial data response 0x%02X after transfer cancelled, ignoring.", data);
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

