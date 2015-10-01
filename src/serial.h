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
    uint8 GetSerialData() const { return m_serial_read_data; }
    void SetSerialControl(uint8 value);
    void SetSerialData(uint8 value);

    // reset
    void Reset();

    // step
    void ExecuteFor(uint32 clocks);

private:
    uint32 GetTransferClocks() const;
    void SendNotReadyResponse();
    void EndTransfer(uint32 clocks);
    void HandleRequests();

    System *m_system;
    bool m_has_connection;

    // serial
    uint8 m_serial_control;
    uint8 m_serial_read_data;
    uint8 m_serial_write_data;

    // clock sequence number
    uint32 m_sequence;
    uint32 m_expected_sequence;
    uint32 m_external_clocks;

    // pending data
    uint32 m_serial_wait_clocks;
    uint32 m_clocks_since_transfer_start;
    uint32 m_nonready_clocks;
    uint32 m_nonready_sequence;
};
