#pragma once
#include "YBaseLib/Common.h"
#include "system.h"

class String;
class ByteStream;
class BinaryReader;
class BinaryWriter;
class Error;

class Cartridge;

#define ACCURATE_MEMORY_TIMING 1

class CPU
{
    friend System;

public:
    enum Reg8
    {
        Reg8_F,
        Reg8_A,
        Reg8_C,
        Reg8_B,
        Reg8_E,
        Reg8_D,
        Reg8_L,
        Reg8_H,
        Reg8_SP_LSB,
        Reg8_SP_MSB,
        Reg8_PC_LSB,
        Reg8_PC_MSB,
        NumReg8
    };

    enum Reg16
    {
        Reg16_AF,
        Reg16_BC,
        Reg16_DE,
        Reg16_HL,
        Reg16_SP,
        Reg16_PC,
        NumReg16
    };

    enum REG8
    {
        REG8_F,
        REG8_A,
        REG8_C,
        REG8_B,
        REG8_E,
        REG8_D,
        REG8_L,
        REG8_H,
        NUM_REG8
    };

    enum REG16
    {
        REG16_AF,
        REG16_BC,
        REG16_DE,
        REG16_HL,
        REG16_SP,
        REG16_PC,
        NUM_REG16
    };

    enum FLAG
    {
        FLAG_Z = (1 << 7),
        FLAG_N = (1 << 6),
        FLAG_H = (1 << 5),
        FLAG_C = (1 << 4),
    };

    struct Registers
    {
        union 
        {
            // direct register access
            struct
            {
                union
                {
                    struct
                    {
                        uint8 F;
                        uint8 A;
                    };
                    uint16 AF;
                };
                union
                {
                    struct
                    {
                        uint8 C;
                        uint8 B;
                    };
                    uint16 BC;
                };
                union
                {
                    struct
                    {
                        uint8 E;
                        uint8 D;
                    };
                    uint16 DE;
                };
                union
                {
                    struct
                    {
                        uint8 L;
                        uint8 H;
                    };
                    uint16 HL;
                };
                uint16 SP;
                uint16 PC;
            };

            // 8-bit access
            uint8 reg8[NUM_REG8];

            // 16-bit access
            uint16 reg16[NUM_REG16];
        };

        // Interrupt master enable
        bool IME;

        // Interrupt enabled
        uint8 IE;

        // Interrupt requests
        uint8 IF;

        bool GetFlagZ() const { return ((F & FLAG_Z) != 0); }
        bool GetFlagN() const { return ((F & FLAG_N) != 0); }
        bool GetFlagH() const { return ((F & FLAG_H) != 0); }
        bool GetFlagC() const { return ((F & FLAG_C) != 0); }

        void SetFlagZ(bool on) { if (on) { F |= FLAG_Z; } else { F &= ~FLAG_Z; } }
        void SetFlagN(bool on) { if (on) { F |= FLAG_N; } else { F &= ~FLAG_N; } }
        void SetFlagH(bool on) { if (on) { F |= FLAG_H; } else { F &= ~FLAG_H; } }
        void SetFlagC(bool on) { if (on) { F |= FLAG_C; } else { F &= ~FLAG_C; } }

        void UpdateFlagZ(uint8 value) { SetFlagZ((value == 0)); }
    };

public:
    CPU(System *memory);
    ~CPU();

    // register access
    const Registers *GetRegisters() const { return &m_registers; }
    Registers *GetRegisters() { return &m_registers; }
    const uint32 GetCycles() const { return m_clock; }

    // reset
    void Reset();

    // step
    void ExecuteInstruction();
    void ExecuteInstructionOld();

    // disassemble an instruction
    static bool Disassemble(String *pDestination, System *memory, uint16 address);
    static void DisassembleFrom(System *system, uint16 address, uint16 count, ByteStream *pStream);

private:
    // memory read/writes
    inline void DelayCycle() { m_system->AddCPUCycles(4); }
    inline void DelayCycles(uint32 count) { m_system->AddCPUCycles(count); }
    inline uint8 MemReadByte(uint16 address) { DelayCycle(); return m_system->CPURead(address); }
    inline void MemWriteByte(uint16 address, uint8 value) { DelayCycle(); m_system->CPUWrite(address, value); }
    inline uint16 MemReadWord(uint16 address) { return ((uint16)MemReadByte(address)) | ((uint16)MemReadByte(address + 1) << 8); }
    inline void MemWriteWord(uint16 address, uint16 value) { MemWriteByte(address, (uint8)(value)); MemWriteByte(address + 1, (uint8)(value >> 8)); }

    // stack push/pop with protection
    void Push(uint8 value);
    uint8 PopByte();
    void PushWord(uint16 value);
    uint16 PopWord();

    // raise interrupt
    void RaiseInterrupt(uint8 index);

    // halt cycles
    void Disable(bool disabled);

    // state saving
    bool LoadState(ByteStream *pStream, BinaryReader &binaryReader, Error *pError);
    void SaveState(ByteStream *pStream, BinaryWriter &binaryWriter);
    
    // registers
    Registers m_registers;

    // memory
    System *m_system;

    // clock values
    uint32 m_clock;

    // halted, waiting for interrupt
    bool m_halted;

    // halted during memory transfer, cannot break out of this
    bool m_disabled;

private:
    uint8 ReadOperandByte();
    uint16 ReadOperandWord();
    int8 ReadOperandSignedByte();
    uint8 INSTR_inc(uint8 value);
    uint8 INSTR_dec(uint8 value);
    void INSTR_add(uint8 value); // add a, value
    void INSTR_adc(uint8 value); // adc a, value
    void INSTR_sub(uint8 value); // sub a, value
    void INSTR_sbc(uint8 value); // sbc a, value
    void INSTR_and(uint8 value); // and a, value
    void INSTR_or(uint8 value); // or a, value
    void INSTR_xor(uint8 value); // xor a, value
    void INSTR_cp(uint8 value); // cp a, value
    uint8 INSTR_rl(uint8 value, bool set_z);
    uint8 INSTR_rr(uint8 value, bool set_z);
    uint8 INSTR_rlc(uint8 value, bool set_z);
    uint8 INSTR_rrc(uint8 value, bool set_z);
    uint8 INSTR_sla(uint8 value);
    uint8 INSTR_sra(uint8 value);
    uint8 INSTR_srl(uint8 value);
    uint8 INSTR_swap(uint8 value);
    void INSTR_bit(uint8 bit, uint8 value);
    uint8 INSTR_res(uint8 bit, uint8 value);
    uint8 INSTR_set(uint8 bit, uint8 value);
    void INSTR_halt();
    void INSTR_stop();
    void INSTR_jr(int8 displacement);
    void INSTR_jp(uint16 address);
    void INSTR_call(uint16 address);
    void INSTR_ret();
    void INSTR_rst(uint8 vector); // RST vector
    void INSTR_addhl(uint16 value);
    void INSTR_addsp(int8 displacement);
    void INSTR_ldhlsp(int8 displacement);
    void INSTR_daa();
};


