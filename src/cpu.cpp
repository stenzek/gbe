#include "cpu.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/String.h"
#include "YBaseLib/Log.h"
Log_SetChannel(CPU);

CPU::CPU(System *system)
    : m_system(system)
{
    Reset();
}

CPU::~CPU()
{

}

void CPU::Reset()
{
    // zero all registers
    Y_memzero(&m_registers, sizeof(m_registers));

    // enable master interrupts, but keep all interrupts blocked
    m_registers.IME = true;
    m_clock = 0;
    m_halted = false;
}

void CPU::Push(uint8 value)
{
    DebugAssert(m_registers.SP > 0);
    m_registers.SP--;
    m_system->CPUWrite(m_registers.SP, value);
}

void CPU::PushWord(uint16 value)
{
    DebugAssert(m_registers.SP >= 0xC002);
    //m_registers.SP--;
    //m_mmu->Write8(m_registers.SP, (value >> 8) & 0xFF);
    //m_registers.SP--;
    //m_mmu->Write8(m_registers.SP, value & 0xFF);
    m_registers.SP -= 2;
    MemWriteWord(m_registers.SP, value);
}

uint8 CPU::PopByte()
{
    DebugAssert(m_registers.SP < 0xFFFF);
    uint8 value = MemReadByte(m_registers.SP);
    m_registers.SP++;
    return value;
}

uint16 CPU::PopWord()
{
    DebugAssert(m_registers.SP < 0xFFFE);

    //uint16 value = (uint16)(m_mmu->Read8(m_registers.SP + 1) << 8) | (uint16)(m_mmu->Read8(m_registers.SP));
    uint16 value = MemReadWord(m_registers.SP);
    m_registers.SP += 2;
    return value;
}

void CPU::RaiseInterrupt(uint8 index)
{
    DebugAssert(index < NUM_CPU_INT);

    //Log_DevPrintf("Raise interrupt %u", index);
    m_registers.IF |= (1 << index);
    m_halted = false;
}

bool CPU::TestPredicate(Instruction::Predicate condition)
{
    switch (condition)
    {
    case Instruction::Predicate_Always:
        return true;

    case Instruction::Predicate_Zero:
        return m_registers.GetFlagZ();

    case Instruction::Predicate_Carry:
        return m_registers.GetFlagC();

    case Instruction::Predicate_NotZero:
        return !m_registers.GetFlagZ();

    case Instruction::Predicate_NotCarry:
        return !m_registers.GetFlagC();

    default:
        UnreachableCode();
        return false;
    }
}

uint64 counts[256] = { 0 };
uint64 cb_counts[256] = { 0 };

uint32 CPU::Step()
{
    // interrupts enabled?
    if (m_registers.IME)
    {
        // have we got a pending interrupt?
        uint8 interrupt_mask = ((1 << (NUM_CPU_INT)) - 1) & m_registers.IF & m_registers.IE;
        if (interrupt_mask != 0)
        {
            // http://bgb.bircd.org/pandocs.htm#interrupts
            // find the first interrupt pending in priority (0 = highest)
            for (uint32 i = 0; i < NUM_CPU_INT; i++)
            {
                if (interrupt_mask & (1 << i))
                {
                    // trigger this interrupt
                    // clear flag
                    m_registers.IF &= ~(1 << i);

                    // disable interrupts
                    m_registers.IME = false;

                    // Jump to vector
                    static const uint16 jump_locations[] = {
                        0x0040,     // vblank
                        0x0048,     // lcdc
                        0x0050,     // timer
                        0x0058,     // serial
                        0x0060,     // joypad
                    };

                    Log_DevPrintf("Entering interrupt handler $%04X, PC was $%04X", jump_locations[i], m_registers.PC);
                    //DisassembleFrom(m_system, m_registers.PC, 10);

                    PushWord(m_registers.PC);
                    m_registers.PC = jump_locations[i];
                    m_halted = false;
                    break;
                }
            }
        }
    }

    // if halted, simulate a single cycle to keep the display/audio going
    if (m_halted)
        return 4;

//     if (m_registers.PC == 0xc31a)
//         __debugbreak();

    // debug
    static bool disasm_enabled = false;
    //static bool disasm_enabled = true;
    if (disasm_enabled)
    {
        SmallString disasm;
        if (Disassemble(&disasm, m_system, m_registers.PC))
            Log_DevPrintf("exec: %s", disasm.GetCharArray());
        else
            Log_DevPrintf("disasm fail at %04X", m_registers.PC);
    }

    // fetch opcode
    uint16 original_pc = m_registers.PC;
    uint8 instruction_buffer[3] = { 0 };
    instruction_buffer[0] = MemReadByte(m_registers.PC++);

    // decode opcode - if we wanted we could count cycles here to read
    const Instruction *instruction = &instructions[instruction_buffer[0]];
    counts[instruction_buffer[0]]++;

    // handle prefixed instructions
    if (instruction->type == Instruction::Type_Prefix)
    {
        uint8 prefix = instruction_buffer[0];
        instruction_buffer[0] = MemReadByte(m_registers.PC++);
        switch (prefix)
        {
        case 0xCB:
            instruction = &cb_instructions[instruction_buffer[0]];
            cb_counts[instruction_buffer[0]]++;
            break;

        default:
            UnreachableCode();
        }

        // read extra bytes
        if (instruction->length > 2)
            instruction_buffer[1] = MemReadByte(m_registers.PC++);
    }

    // grab extra bytes
    else if (instruction->length > 1)
    {
        instruction_buffer[1] = MemReadByte(m_registers.PC++);
        if (instruction->length > 2)
            instruction_buffer[2] = MemReadByte(m_registers.PC++);
    }

    // check for stub instructions
    if (instruction->type == Instruction::Type_Stub)
    {
        SmallString disasm;
        if (Disassemble(&disasm, m_system, original_pc))
            Log_ErrorPrintf("instruction not implemented: %s", disasm.GetCharArray());
        else
            Log_ErrorPrintf("disasm fail at %04X", original_pc);

        Panic("instruction not implemented");
    }

    // get source/destination operands
    const Instruction::Operand *operand = &instruction->operand;
    const Instruction::Operand *destination = &instruction->operand;
    const Instruction::Operand *source = &instruction->operand2;

    // helper macros for reading immediate values
    #define get_imm8() (instruction_buffer[1])
    #define get_imm16() ((uint16)instruction_buffer[1] | ((uint16)instruction_buffer[2] << 8))

    // execution table
    uint32 cycles_consumed = instruction->cycles;
    switch (instruction->type)
    {
        //////////////////////////////////////////////////////////////////////////
        // Null operation
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Nop:
        {
            // do nothing :)
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Move (one register -> another register)
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Move:
        {
            DebugAssert(source->mode == destination->mode);
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Reg16:
                m_registers.reg16[destination->reg16] = m_registers.reg16[source->reg16];
                break;

            default:
                UnreachableCode();
            }

            break;
        }


        //////////////////////////////////////////////////////////////////////////
        // Load
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Load:
        {
            // load is always memory/immediate -> register. 16-bit loads from immediates exist.
            switch (destination->mode)
            {
                // 8-bit load
            case Instruction::AddressMode_Reg8:
                {
                    uint8 value = 0;
                    switch (source->mode)
                    {
                    case Instruction::AddressMode_Imm8:
                        value = get_imm8();
                        break;

                    case Instruction::AddressMode_Mem16:
                        {
                            value = MemReadByte(m_registers.reg16[source->reg16]);
                            if (instruction->load_action == Instruction::LoadStoreAction_IncrementAddress)
                                m_registers.reg16[source->reg16]++;
                            else if (instruction->load_action == Instruction::LoadStoreAction_DecrementAddress)
                                m_registers.reg16[source->reg16]--;

                            break;
                        }

                    case Instruction::AddressMode_Addr16:
                        {
                            value = MemReadByte(get_imm16());
                            break;
                        }

                    default:
                        UnreachableCode();
                    }
                    m_registers.reg8[destination->reg8] = value;
                    break;
                }

                // 16-bit load
            case Instruction::AddressMode_Reg16:
                {
                    uint16 value = 0;
                    switch (source->mode)
                    {
                    case Instruction::AddressMode_Imm16:
                        value = get_imm16();
                        break;

                    default:
                        UnreachableCode();
                    }
                    m_registers.reg16[destination->reg16] = value;
                    break;
                }

                // unhandled
            default:
                UnreachableCode();
            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Store
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Store:
        {
            switch (source->mode)
            {
                // 8-bit store
            case Instruction::AddressMode_Reg8:
            case Instruction::AddressMode_Imm8:
                {
                    // register -> memory
                    uint8 value = (source->mode == Instruction::AddressMode_Reg8) ? m_registers.reg8[source->reg8] : (get_imm8());
                    switch (destination->mode)
                    {
                    case Instruction::AddressMode_Mem16:
                        {
                            MemWriteByte(m_registers.reg16[destination->reg16], value);
                            if (instruction->load_action == Instruction::LoadStoreAction_IncrementAddress)
                                m_registers.reg16[destination->reg16]++;
                            else if (instruction->load_action == Instruction::LoadStoreAction_DecrementAddress)
                                m_registers.reg16[destination->reg16]--;

                            break;
                        }

                    case Instruction::AddressMode_Addr16:
                        MemWriteByte(get_imm16(), value);
                        break;

                    default:
                        UnreachableCode();
                    }

                    break;
                }

                // 16-bit store
            case Instruction::AddressMode_Reg16:
                {
                    uint16 value = m_registers.reg16[source->reg16];
                    switch (destination->mode)
                    {
                    case Instruction::AddressMode_Addr16:
                        MemWriteWord(get_imm16(), value);
                        break;

                    default:
                        UnreachableCode();
                    }

                    break;
                }

                // unhandled
            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_READIO:
        {
            // get register offset
            uint8 regnum = 0;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                regnum = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                regnum = m_registers.reg8[operand->reg8];
                break;

            default:
                UnreachableCode();
            }

            // read memory
            m_registers.A = m_system->CPUReadIORegister(regnum);
            break;
        }

    case Instruction::Type_WRITEIO:
        {
            // get register offset
            uint8 regnum = 0;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                regnum = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                regnum = m_registers.reg8[operand->reg8];
                break;

            default:
                UnreachableCode();
            }

            // write memory
            m_system->CPUWriteIORegister(regnum, m_registers.A);
            break;
        }

    case Instruction::Type_INC:
        {
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                {
                    // 8-bit register increment
                    uint8 old_value = m_registers.reg8[operand->reg8];
                    uint8 value = old_value + 1;
                    m_registers.SetFlagZ(value == 0);
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((old_value & 0xF) == 0xF);
                    m_registers.reg8[operand->reg8] = value;
                    break;
                }

            case Instruction::AddressMode_Mem16:
                {
                    // 8-bit memory increment
                    uint16 address = m_registers.reg16[operand->reg16];
                    uint8 old_value = MemReadByte(address);
                    uint8 value = old_value + 1;
                    m_registers.SetFlagZ(value == 0);
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((old_value & 0xF) == 0xF);
                    MemWriteByte(address, value);
                    break;
                }

            case Instruction::AddressMode_Reg16:
                {
                    // 16-bit register increment does not alter flags
                    m_registers.reg16[operand->reg16]++;
                    break;
                }

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_DEC:
        {
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                {
                    // 8-bit register decrement
                    uint8 value = --m_registers.reg8[operand->reg8];
                    m_registers.SetFlagZ(value == 0);
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((value & 0xF) == 0xF);
                    break;
                }

            case Instruction::AddressMode_Mem16:
                {
                    // 8-bit memory decrement
                    uint16 address = m_registers.reg16[operand->reg16];
                    uint8 value = MemReadByte(address) - 1;
                    m_registers.SetFlagZ(value == 0);
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((value & 0xF) == 0xF);
                    MemWriteByte(address, value);
                    break;
                }

            case Instruction::AddressMode_Reg16:
                {
                    // 16-bit register decrement does not alter flags
                    m_registers.reg16[operand->reg16]--;
                    break;
                }

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_ADD:
        {
            // get value to add
            uint32 addend;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                addend = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                addend = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                addend = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                addend = 0;
            }

            // store value - only writes to A
            uint8 old_value = m_registers.A;
            uint32 new_value = old_value + addend;
            m_registers.A = (uint8)(new_value & 0xFF);
            m_registers.SetFlagZ(((new_value & 0xFF) == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(((old_value & 0xF) + (addend & 0xF)) > 0xF);
            m_registers.SetFlagC(new_value > 0xFF);
            break;
        }

    case Instruction::Type_ADD16:
        {
            // 16-bit add of 2 registers
            DebugAssert(source->mode == Instruction::AddressMode_Reg16 && destination->mode == Instruction::AddressMode_Reg16);
            uint16 old_value = m_registers.reg16[destination->reg16];
            uint16 addend = m_registers.reg16[source->reg16];
            uint32 new_value = old_value + (uint32)addend;
            m_registers.reg16[destination->reg16] = new_value & 0xFFFF;
            m_registers.SetFlagN(false);
            m_registers.SetFlagH((new_value & 0xFFF) < ((uint32)old_value & 0xFFF));
            m_registers.SetFlagC(new_value > 0xFFFF); // correct?
            break;
        }

    case Instruction::Type_ADDS8:
        {
            // Add 8-bit signed value to R16
            uint16 old_value = m_registers.reg16[destination->reg16];
            uint32 new_value;

            // can be signed
            int8 r8 = (int8)get_imm8();
            if (r8 < 0)
                new_value = old_value - (uint16)(-r8);
            else
                new_value = old_value + (uint16)r8;

            // clears zero flag for some reason (but reg+reg doesn't)
            m_registers.reg16[destination->reg16] = new_value & 0xFFFF;
            m_registers.SetFlagZ(false);
            m_registers.SetFlagN(false);
            m_registers.SetFlagH((new_value & 0xFFF) < ((uint32)old_value & 0xFFF));
            m_registers.SetFlagC(new_value > 0xFFFF); // correct?
            break;
        }

    case Instruction::Type_ADC:
        {
            // get value to add
            uint8 addend;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                addend = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                addend = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                addend = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                addend = 0;
            }

            // handle carry
            uint8 carry_in = (uint8)m_registers.GetFlagC();

            // do operation
            uint8 old_value = m_registers.A;
            uint8 new_value = old_value + addend + carry_in;
            m_registers.A = new_value;

            // update flags
            m_registers.SetFlagZ((new_value == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(((old_value & 0xF) + (addend & 0xF) + carry_in) > 0xF);
            m_registers.SetFlagC(((uint32)old_value + addend + carry_in) > 0xFF);
            break;
        }

    case Instruction::Type_SUB:
        {
            // get value to subtract
            uint8 addend;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                addend = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                addend = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                addend = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                addend = 0;
            }

            // store value - only writes to A
            uint8 old_value = m_registers.A;
            uint8 new_value = old_value - addend;
            m_registers.A = new_value;

            m_registers.SetFlagZ((new_value == 0));
            m_registers.SetFlagN(true);
            m_registers.SetFlagH((new_value & 0xF) > (old_value & 0xF));
            m_registers.SetFlagC(addend > old_value);
            break;
        }

    case Instruction::Type_SBC:
        {
            // get value to subtract
            uint8 addend;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                addend = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                addend = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                addend = MemReadByte(m_registers.reg16[source->reg16]);
                break;

            default:
                UnreachableCode();
                addend = 0;
            }

            // handle carry
            uint8 carry_in = (uint8)m_registers.GetFlagC();
            uint8 old_value = m_registers.A;
            uint8 new_value = old_value - addend - carry_in;
            m_registers.A = new_value;
              
            // update flags
            m_registers.SetFlagZ((new_value == 0));
            m_registers.SetFlagN(true);
            m_registers.SetFlagH(((addend & 0xF) + carry_in) > (old_value & 0xF));
            m_registers.SetFlagC((addend + carry_in) > old_value);
            break;
        }

    case Instruction::Type_AND:
        {
            // get value
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            case Instruction::AddressMode_Imm8:
                value = get_imm8();
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // AND accumulator with value
            m_registers.A &= value;
            
            // update flags
            m_registers.SetFlagZ((m_registers.A == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(true);
            m_registers.SetFlagC(false);
            break;
        }

    case Instruction::Type_OR:
        {
            // get value
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            case Instruction::AddressMode_Imm8:
                value = get_imm8();
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // OR accumulator with value
            m_registers.A |= value;

            // update flags
            m_registers.SetFlagZ((m_registers.A == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(false);
            break;
        }

    case Instruction::Type_XOR:
        {
            // get value
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            case Instruction::AddressMode_Imm8:
                value = get_imm8();
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // XOR accumulator with value
            m_registers.A ^= value;
            m_registers.SetFlagZ((m_registers.A == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(false);
            break;
        }

    case Instruction::Type_RL:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // 9-bit rotation, carry -> bit 0
            uint8 old_value = value;
            value = (value << 1) | (uint8)m_registers.GetFlagC();

            // update flags
            m_registers.SetFlagC(!!(old_value & 0x80));
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // non-prefixed rotates zero z flag
            if (instruction->length > 1)
                m_registers.SetFlagZ((value == 0));
            else
                m_registers.SetFlagZ(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_RR:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // 9-bit rotation, carry -> bit 7
            uint8 old_value = value;
            value = (value >> 1) | ((uint8)m_registers.GetFlagC() << 7);

            // update flags
            m_registers.SetFlagC(!!(old_value & 0x01));
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // non-prefixed rotates zero z flag
            if (instruction->length > 1)
                m_registers.SetFlagZ((value == 0));
            else
                m_registers.SetFlagZ(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_RLC:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // bit 7 -> carry
            m_registers.SetFlagC((value & 0x80) != 0);

            // rotate to left
            value = ((value & 0x80) >> 7) | (value << 1);

            // update flags
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // non-prefixed rotates zero z flag
            if (instruction->length > 1)
                m_registers.SetFlagZ((value == 0));
            else
                m_registers.SetFlagZ(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_RRC:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // bit 0 -> carry
            m_registers.SetFlagC((value & 0x01) != 0);

            // rotate to right
            value = ((value & 0x01) << 7) | (value >> 1);

            // update flags
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // non-prefixed rotates zero z flag
            if (instruction->length > 1)
                m_registers.SetFlagZ((value == 0));
            else
                m_registers.SetFlagZ(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_CPL:
        {
            m_registers.A = ~m_registers.A;
            m_registers.SetFlagN(true);
            m_registers.SetFlagH(true);
            break;
        }

    case Instruction::Type_SWAP:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // swap nibbles
            value = (value << 4) | (value >> 4);

            // write value
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
                value = 0;
            }
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Compare
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_CP:
        {
            // get rhs
            uint8 comparand = 0;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm8:
                comparand = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                comparand = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                comparand = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
            }

            // implemented in hardware as a subtraction?
            m_registers.SetFlagZ(m_registers.A == comparand);
            m_registers.SetFlagN(true);
            m_registers.SetFlagH((m_registers.A & 0xF) < (comparand & 0xF));
            m_registers.SetFlagC(m_registers.A < comparand);
            break;
        }


        //////////////////////////////////////////////////////////////////////////
        // Test Bit
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_BIT:
        {
            uint8 mask = (1 << instruction->bitnum);
            uint8 value = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[source->reg16]);
                break;

            default:
                UnreachableCode();
                break;
            }

            value &= mask;
            m_registers.SetFlagZ((value == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(true);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Reset Bit
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_SET:
        {
            uint8 mask = (1 << instruction->bitnum);
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] |= mask;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[destination->reg16], MemReadByte(m_registers.reg16[destination->reg16]) | mask);
                break;
            default:
                UnreachableCode();
                break;

            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Reset Bit
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_RES:
        {
            uint8 mask = (1 << instruction->bitnum);
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] &= ~mask;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[destination->reg16], MemReadByte(m_registers.reg16[destination->reg16]) & ~mask);
                break;

            default:
                UnreachableCode();
                break;
            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Jump Absolute
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_JP:
        {
            // get jump location
            uint16 address;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Imm16:
                address = get_imm16();
                break;

            case Instruction::AddressMode_Reg16:
                address = m_registers.reg16[operand->reg16];
                break;

            default:
                UnreachableCode();
                address = 0;
            }

            if (TestPredicate(instruction->predicate))
                m_registers.PC = address;
            else
                cycles_consumed = instruction->cycles_skipped;

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Jump Relative
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_JR:
        {
            if (TestPredicate(instruction->predicate))
            {
                int8 d8 = (int8)get_imm8();
                if (d8 < 0)
                    m_registers.PC -= (uint16)-d8;
                else
                    m_registers.PC += (uint16)d8;
            }
            else
            {
                cycles_consumed = instruction->cycles_skipped;
            }

            break;
        }
        break;

        //////////////////////////////////////////////////////////////////////////
        // Call
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_CALL:
        {
            if (TestPredicate(instruction->predicate))
            {
                PushWord(m_registers.PC);
                m_registers.PC = get_imm16();
            }
            else
            {
                cycles_consumed = instruction->cycles_skipped;
            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Return
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_RET:
        {
            if (TestPredicate(instruction->predicate))
                m_registers.PC = PopWord();
            else
                cycles_consumed = instruction->cycles_skipped;

            break;
        }

    case Instruction::Type_RETI:
        {
            //DebugAssert(!m_registers.IME);
            //Log_DevPrintf("Interrupts enabled from RETI");
            m_registers.IME = true;
            m_registers.PC = PopWord();
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Push
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_PUSH:
        {
            DebugAssert(source->mode == Instruction::AddressMode_Reg16);
            PushWord(m_registers.reg16[source->reg16]);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Pop
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_POP:
        {
            DebugAssert(destination->mode == Instruction::AddressMode_Reg16);
            m_registers.reg16[destination->reg16] = PopWord();

            // POP AF drops lower 4 bits
            if (destination->reg16 == Reg16_AF)
                m_registers.AF &= 0xFFF0;

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Restart
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_RST:
        {
            PushWord(m_registers.PC);
            m_registers.PC = 0x0000 + destination->restart_vector;
            break;
        }

    case Instruction::Type_SCF:
        {
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(true);
            break;
        }

    case Instruction::Type_CCF:
        {
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(!m_registers.GetFlagC());
            break;
        }

    case Instruction::Type_HALT:
        {
            m_halted = true;
            //Log_DevPrintf("CPU Halt at %04X", original_pc);
            break;
        }

    case Instruction::Type_STOP:
        {
            m_halted = true;
            Log_DevPrintf("CPU Stop");
            break;
        }

    case Instruction::Type_EI:
        {
            m_registers.IME = true;
            //Log_DevPrintf("Interrupts enabled from EI");
            break;
        }

    case Instruction::Type_DI:
        {
            m_registers.IME = false;
            //Log_DevPrintf("Interrupts disabled from DI");
            break;
        }

    case Instruction::Type_LDHL_SPR8:
        {
            uint16 value = m_registers.SP;
            uint16 old_value = value;
            int8 offset = (int8)get_imm8();
            if (offset < 0)
                value -= -offset;
            else
                value += offset;

            m_registers.HL = value;

            // affects flags, only load that does. how??
            m_registers.SetFlagZ(false);
            m_registers.SetFlagN(false);
            m_registers.SetFlagH((value & 0xF) < (old_value & 0xF));
            m_registers.SetFlagC((value & 0xFF) < (old_value & 0xFF));
            break;
        }

        // shift left arithmetic
    case Instruction::Type_SLA:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // shift to left, bit 7 -> carry, bit 0 <- 0
            m_registers.SetFlagC((value & 0x80) != 0);
            value <<= 1;

            // update flags
            m_registers.SetFlagZ((value == 0));
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

        // shift right arithmetic
    case Instruction::Type_SRA:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // shift to right, keep bit 7, bit 1 -> carry
            m_registers.SetFlagC((value & 0x01) != 0);
            value = (value & 0x80) | (value >> 1);

            // update flags
            m_registers.SetFlagZ((value == 0));
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

        // shift right logical
    case Instruction::Type_SRL:
        {
            uint8 value;
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[operand->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[operand->reg16]);
                break;

            default:
                UnreachableCode();
                value = 0;
            }

            // shift to right, bit 7 <- 0, bit 0 -> carry
            m_registers.SetFlagC((value & 0x01));
            value >>= 1;

            // update flags
            m_registers.SetFlagZ((value == 0));
            m_registers.SetFlagH(false);
            m_registers.SetFlagN(false);

            // write back
            switch (operand->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[operand->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[operand->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

    case Instruction::Type_DAA:
        {
            uint16 value = m_registers.A;
            if (m_registers.GetFlagN())
            {
                if (m_registers.GetFlagH())
                    value = (value - 0x06) & 0xFF;
                if (m_registers.GetFlagC())
                    value -= 0x60;
            }
            else
            {
                if (m_registers.GetFlagH() || (value & 0xF) > 9)
                    value += 0x06;
                if (m_registers.GetFlagC() || value > 0x9F)
                    value += 0x60;
            }

            m_registers.A = value & 0xFF;
            m_registers.SetFlagH(false);
            m_registers.SetFlagZ((m_registers.A == 0));
            if (value > 0xFF)
                m_registers.SetFlagC(true);

            break;
        }

    default:
        UnreachableCode();
        break;

    }

//     if ((m_registers.PC == 0x0000 || m_registers.PC >= 0x4000) && (m_registers.PC < 0xFF80 || m_registers.PC > 0xFF90))
//         __debugbreak();

    #undef get_imm8
    #undef get_imm16
    return cycles_consumed;
}

