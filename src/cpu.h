#pragma once
#include "YBaseLib/Common.h"
#include "system.h"

class String;
class Cartridge;

class CPU
{
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
    uint32 Step();

    // disassemble an instruction
    static bool Disassemble(String *pDestination, System *memory, uint16 address);

private:
    // memory read/writes
    inline uint8 MemReadByte(uint16 address) { return m_system->CPURead(address); }
    inline void MemWriteByte(uint16 address, uint8 value) { m_system->CPUWrite(address, value); }
    inline uint16 MemReadWord(uint16 address) { return ((uint16)m_system->CPURead(address)) | ((uint16)m_system->CPURead(address + 1) << 8); }
    inline void MemWriteWord(uint16 address, uint16 value) { m_system->CPUWrite(address, (uint8)(value)); m_system->CPUWrite(address + 1, (uint8)(value >> 8)); }

    // stack push/pop with protection
    void Push(uint8 value);
    uint8 PopByte();
    void PushWord(uint16 value);
    uint16 PopWord();
    
    // registers
    Registers m_registers;

    // memory
    System *m_system;

    // clock values
    uint32 m_clock;

    // halted, waiting for interrupt
    bool m_halted;

public:

    struct Instruction
    {
        enum AddressMode
        {
            AddressMode_Reg8,
            AddressMode_Reg16,
            AddressMode_Imm8,
            AddressMode_Imm16,
            AddressMode_Mem8,
            AddressMode_Mem16,
            AddressMode_Addr8,
            AddressMode_Addr16,
            AddressMode_Stack,
            NumAddressModes
        };

        enum Type
        {
            Type_Stub,
            Type_Prefix,
            Type_Nop,
            Type_Load,
            Type_Store,
            Type_ReadIOReg,
            Type_WriteIOReg,
            Type_Move,
            Type_JumpRelative,
            Type_JumpAbsolute,
            Type_Call,
            Type_Return,
            Type_Push,
            Type_Pop, 
            Type_Increment,
            Type_Decrement,
            Type_Add,
            Type_Sub,
            Type_And,
            Type_Or,
            Type_Xor,
            Type_Not,
            Type_Swap,
            Type_Rotate,
            Type_Cmp,
            Type_TestBit,
            Type_SetBit,
            Type_ResetBit,
            Type_Restart,
            Type_Untyped,
        };

        enum Untyped
        {
            Untyped_CCF,
            Untyped_SCF,
            Untyped_HALT,
            Untyped_STOP,
            Untyped_EI,
            Untyped_DI,
        };

        enum LoadStoreAction
        {
            LoadStoreAction_None,
            LoadStoreAction_IncrementAddress,
            LoadStoreAction_DecrementAddress,
        };

        enum Predicate
        {
            Predicate_Always,
            Predicate_Zero,
            Predicate_Carry,
            Predicate_NotZero,
            Predicate_NotCarry,
            Predicate_FromInterrupt,
        };

        enum CarryAction
        {
            CarryAction_With,
            CarryAction_Ignore
        };

        enum RotateDirection
        {
            RotateDirection_Left,
            RotateDirection_Right
        };

        struct Operand
        {
            AddressMode mode;
            union
            {
                Reg8 reg8;
                Reg16 reg16;
                RotateDirection direction;
            };
        };

        Type type;
        Operand destination;
        Operand source;
        uint32 length;
        uint32 cycles;
        union
        {
            LoadStoreAction load_action;
            Predicate predicate;
            CarryAction carry;
            Untyped untyped_opcode;
            uint8 bitnum;
            uint8 restart_vector;
        };
        uint32 cycles_skipped;
    };

private:
    static const Instruction instructions[256];
    static const Instruction cb_instructions[256];

    bool TestPredicate(Instruction::Predicate condition);
};
