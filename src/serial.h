#pragma once
#include "structures.h"
#include "system.h"

class LinkSocket;

class Serial
{
public:
    Serial(System *system);
    ~Serial();

    // Registers
    uint8 GetSerialControl() const { return m_serial_control; }
    uint8 GetSerialData() const { return m_serial_data; }
    void SetSerialControl(uint8 value);
    void SetSerialData(uint8 value);

    // reset
    void Reset();

    // step
    void ExecuteFor(uint32 clocks);

private:
    uint32 GetTransferClocks() const;
    void PollSocket(LinkSocket *socket);

    System *m_system;
    uint32 m_clocks_since_transfer_start;
    uint32 m_serial_receive_clock;

    // serial
    uint8 m_serial_control;
    uint8 m_serial_data;

    // pending data
    uint8 m_serial_send_buffer;
    uint8 m_serial_receive_buffer;
    bool m_serial_has_send_buffer;
    bool m_serial_has_receive_buffer;

    uint8 m_serial_pending_data;
    uint32 m_serial_pending_data_clocks;
    bool m_serial_pending_ack;
};
