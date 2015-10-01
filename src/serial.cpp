#include "serial.h"
#include "system.h"
#include "link.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/NumericLimits.h"
Log_SetChannel(Serial);

Serial::Serial(System *system)
    : m_system(system)
    , m_has_connection(false)
    , m_serial_control(0x00)
    , m_serial_read_data(0xFF)
    , m_serial_write_data(0xFF)
    , m_sequence(0)
    , m_expected_sequence(Y_UINT32_MAX)
    , m_external_clocks(0)
    , m_clocks_since_transfer_start(0)
    , m_serial_wait_clocks(0)
    , m_nonready_clocks(0)
    , m_nonready_sequence(0)
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

void Serial::SendNotReadyResponse()
{
    // Send the response back immediately.
    WritePacket response(LINK_COMMAND_NOT_READY);
    response << uint32(m_nonready_sequence);
    LinkConnectionManager::GetInstance().SendPacket(&response);

    // Clear state.
    m_nonready_clocks = 0;
    m_nonready_sequence = 0;
    m_serial_read_data = 0xFF;
}

void Serial::SetSerialControl(uint8 value)
{
    uint8 old_value = m_serial_control;
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
            if (m_nonready_clocks > 0)
            {
                Log_DevPrintf("Sending delayed NOTREADY response due to our own clocking.");
                SendNotReadyResponse();
            }

            // Do we have a client?
            if (m_has_connection)
            {
                // Increment sequence number
                TRACE("Serial send sequence %u, data 0x%02X, and clock %u. Pausing system until response.", m_sequence + 1, m_serial_write_data, GetTransferClocks());
                m_sequence++;

                // Send the byte to the client.
                WritePacket packet(LINK_COMMAND_CLOCK);
                packet << uint32(m_sequence) << uint32(GetTransferClocks()) << uint8(m_serial_write_data);
                LinkConnectionManager::GetInstance().SendPacket(&packet);

                // Wait for ACK (i.e. DATA) before simulating.
                m_system->m_serial_pause = true;
                return;
            }

            // No client, or a send error. so just "clock out" nothing
            m_serial_read_data = 0xFF;
            m_serial_wait_clocks = GetTransferClocks();
        }
        else
        {
            TRACE("Waiting for externally clocked data.");

            // Has the other side clocked data out, and we're running late?
            if (m_nonready_clocks > 0)
            {
                TRACE("Sending delayed externally clocked data 0x%02X.", m_sequence, m_serial_write_data);

                // Send the response back immediately.
                WritePacket response(LINK_COMMAND_DATA);
                response << uint32(m_nonready_sequence) << uint8(m_serial_write_data);
                LinkConnectionManager::GetInstance().SendPacket(&response);

                // Assuming incoming data has already been set.
                EndTransfer(m_external_clocks);
                m_nonready_clocks = 0;
                m_nonready_sequence = 0;
            }
        }
    }
    else
    {
        if ((old_value & 0x81) == 0x80)
        {
            TRACE("Cancelling wait for serial data. %u clocks elapsed.", m_clocks_since_transfer_start);
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
    m_expected_sequence = 0;
    m_external_clocks = 0;
    m_serial_wait_clocks = 0;
    m_clocks_since_transfer_start = 0;
    m_nonready_clocks = 0;
    m_nonready_sequence = 0;
}

bool Serial::LoadState(ByteStream *pStream, BinaryReader &binaryReader, Error *pError)
{
    m_serial_control = binaryReader.ReadUInt8();
    m_serial_read_data = binaryReader.ReadUInt8();
    m_serial_write_data = binaryReader.ReadUInt8();

    // reset transfer state when loading state
    m_sequence = 0;
    m_expected_sequence = 0;
    m_external_clocks = 0;
    m_serial_wait_clocks = 0;
    m_clocks_since_transfer_start = 0;
    m_nonready_clocks = 0;
    m_nonready_sequence = 0;

    return true;
}

void Serial::SaveState(ByteStream *pStream, BinaryWriter &binaryWriter)
{
    binaryWriter.WriteUInt8(m_serial_control);
    binaryWriter.WriteUInt8(m_serial_read_data);
    binaryWriter.WriteUInt8(m_serial_write_data);
}

void Serial::EndTransfer(uint32 clocks)
{
    if (m_clocks_since_transfer_start >= clocks)
    {
        TRACE("Late-firing serial interrupt.");

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
    if (clocks > 0)
    {
        // counter
        if (m_serial_control & (1 << 7))
            m_clocks_since_transfer_start += clocks;

        // transfers
        if (m_serial_wait_clocks > 0)
        {
            if (clocks >= m_serial_wait_clocks)
            {
                TRACE("Firing serial interrupt.");
                m_serial_control &= ~(1 << 7);
                m_serial_wait_clocks = 0;
                m_clocks_since_transfer_start = 0;
                m_system->CPUInterruptRequest(CPU_INT_SERIAL);
            }
            else
            {
                m_serial_wait_clocks -= clocks;
            }
        }

        // nonready clocks
        if (m_nonready_clocks > 0)
        {
            if (clocks >= m_nonready_clocks)
            {
                TRACE("Sending delayed NOTREADY response.");
                SendNotReadyResponse();
            }
            else
            {
                m_nonready_clocks -= clocks;
            }
        }
    }

    // link socket activity
    HandleRequests();
}

void Serial::HandleRequests()
{
    // Drain the network thread queue.
    for (;;)
    {
        ReadPacket *packet;
        LinkConnectionManager::LinkState state = LinkConnectionManager::GetInstance().MainThreadPull(&packet);
        if (state == LinkConnectionManager::LinkState_NotConnected)
        {
            // Not connected in the first place.
            m_has_connection = false;
            return;
        }
        else if (state == LinkConnectionManager::LinkState_Disconnected)
        {
            // The link connection was terminated. Restore our state so we continue.
            Log_WarningPrintf("Link connection termination detected.");
            m_has_connection = false;
            m_serial_read_data = 0xFF;
            m_sequence = 0;
            m_expected_sequence = 0;
            m_external_clocks = 0;
            m_serial_wait_clocks = 0;
            m_clocks_since_transfer_start = 0;
            m_nonready_clocks = 0;
            m_nonready_sequence = 0;
            m_system->m_serial_pause = false;
            return;
        }

        // Connected state. Did we get a packet?
        m_has_connection = true;
        if (packet == nullptr)
            return;

        // Handle packet
        switch (packet->GetPacketCommand())
        {
        case LINK_COMMAND_CLOCK:
            {
                uint32 sequence = packet->ReadUInt32();
                uint32 clocks = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();

                // Has our transfer been activated as well? (and with an external clock)
                if ((m_serial_control & 0x81) == 0x80)
                {
                    TRACE("Received sequence (%u), data (0x%02X) and clock (%u), sending response (0x%02X)", sequence, data, clocks, m_serial_write_data);

                    // Send the response back immediately.
                    WritePacket response(LINK_COMMAND_DATA);
                    response << uint32(sequence) << uint8(m_serial_write_data);
                    LinkConnectionManager::GetInstance().SendPacket(&response);

                    // Set the data we received, and end the transfer.
                    m_serial_read_data = data;
                    EndTransfer(clocks);
                }
                else
                {
                    // Wait clocks before sending NOTREADY. This is because the receiving gameboy immediately fires the interrupt upon receiving the clock,
                    // whereas the sender waits for clocks (as it was "paused"). This gives a bit of leeway when assuming latency is consistent, and the
                    // receivers/senders are being swapped with every packet.
                    TRACE("Received sequence (%u) and clock (%u), waiting for a while in case we send data", sequence, clocks);
                    m_external_clocks = clocks;
                    m_nonready_clocks = clocks;
                    m_nonready_sequence = sequence;
                    m_serial_read_data = data;
                }

                break;
            }

        case LINK_COMMAND_DATA:
            {
                // Got a response sent back to us for our clock
                uint32 sequence = packet->ReadUInt32();
                uint8 data = packet->ReadUInt8();

                // Clear pause.
                TRACE("Serial pause CLEARED");
                m_system->m_serial_pause = false;

                // Check control state.
                if ((m_serial_control & 0x81) == 0x81)
                {                    
                    // We have the internal clock. Check the sequence number to ensure this is the correct response.
                    if (sequence == m_sequence)
                    {
                        // Set received data.
                        TRACE("Ending transfer sequence %u with clocked data 0x%02X", sequence, data);
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

        case LINK_COMMAND_NOT_READY:
            {
                // we clocked out and the other side isn't set to transfer
                uint32 sequence = packet->ReadUInt32();

                // Unpause
                TRACE("Serial pause CLEARED");
                m_system->m_serial_pause = false;

                // Check sequence
                if (sequence == m_sequence)
                {
                    TRACE("Ending transfer sequence %u with NOTREADY response.", sequence);

                    // Set data to 0xFF
                    m_serial_read_data = 0xFF;
                    EndTransfer(GetTransferClocks());
                }
                else
                {
                    Log_WarningPrintf("Received serial NOTREADY with incorrect sequence, ignoring.");
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
