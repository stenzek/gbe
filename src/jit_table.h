#include "cpu.h"

namespace JitTable
{
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
            Type_READIO,
            Type_WRITEIO,
            Type_Move,
            Type_JR,
            Type_JP,
            Type_CALL,
            Type_RET,
            Type_RETI,
            Type_PUSH,
            Type_POP, 
            Type_INC,
            Type_DEC,
            Type_INC16,
            Type_DEC16,
            Type_ADD,
            Type_ADD16,
            Type_ADDS8,
            Type_ADC,
            Type_SUB,
            Type_SBC,
            Type_AND,
            Type_OR,
            Type_XOR,
            Type_CPL,
            Type_SWAP,
            Type_RL,
            Type_RR,
            Type_RLC,
            Type_RRC,
            Type_CP,
            Type_BIT,
            Type_SET,
            Type_RES,
            Type_RST,
            Type_CCF,
            Type_SCF,
            Type_HALT,
            Type_STOP,
            Type_EI,
            Type_DI,
            Type_LDHL_SPR8,
            Type_SLA,
            Type_SRA,
            Type_SRL,
            Type_DAA
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
        };

        struct Operand
        {
            AddressMode mode;
            union
            {
                CPU::Reg8 reg8;
                CPU::Reg16 reg16;
                uint8 restart_vector;
            };
        };

        Type type;
        Operand operand;
        Operand operand2;
        uint32 length;
        uint32 cycles;
        union
        {
            LoadStoreAction load_action;
            Predicate predicate;
            uint8 bitnum;
        };
        uint32 cycles_skipped;
    };

    extern const Instruction instructions[256];
    extern const Instruction cb_instructions[256];
}

