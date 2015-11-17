#pragma once
#include "structures.h"
#include "system.h"

class LinkSocket;

class Serial
{
    friend System;

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
    void Synchronize();

private:
    uint32 GetTransferClocks() const;
    void ScheduleSynchronization();
    void SendNotReadyResponse();
    void EndTransfer(uint32 clocks);
    void HandleRequests();

    // state saving
    bool LoadState(ByteStream *pStream, BinaryReader &binaryReader, Error *pError);
    void SaveState(ByteStream *pStream, BinaryWriter &binaryWriter);

    System *m_system;
    uint32 m_last_cycle;
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
