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
    DebugAssert(m_registers.SP > 1);
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

    case Instruction::Predicate_FromInterrupt:
        //DebugAssert(!m_registers.IME);
        m_registers.IME = true;
        Log_DevPrintf("Interrupts enabled from RETI");
        return true;

    default:
        UnreachableCode();
        return false;
    }
}

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

                    PushWord(m_registers.PC);
                    m_registers.PC = jump_locations[i];
                    m_halted = false;

                    Log_DevPrintf("Entering interrupt handler for %u", i);
                    //DisassembleFrom(m_system, m_registers.PC, 10);
                    break;
                }
            }
        }
    }

    // if halted, simulate a single cycle to keep the display/audio going
    if (m_halted)
    {
        if (!m_registers.IME)
            Log_ErrorPrintf("CPU halted with interrupts disabled, real cpu would be frozen until reset");

        return 4;
    }

//     if (m_registers.PC == 0x0203)
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

    // handle prefixed instructions
    if (instruction->type == Instruction::Type_Prefix)
    {
        uint8 prefix = instruction_buffer[0];
        instruction_buffer[0] = MemReadByte(m_registers.PC++);
        switch (prefix)
        {
        case 0xCB:
            instruction = &cb_instructions[instruction_buffer[0]];
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
    const Instruction::Operand *source = &instruction->source;
    const Instruction::Operand *destination = &instruction->destination;

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
                        MemWriteWord(get_imm16(), value);
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

        //////////////////////////////////////////////////////////////////////////
        // Read IO registers
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_ReadIOReg:
        {
            // get register offset
            uint8 regnum = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Imm8:
                regnum = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                regnum = m_registers.reg8[source->reg8];
                break;

            default:
                UnreachableCode();
            }

            // read memory
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8);
            m_registers.reg8[destination->reg8] = m_system->CPUReadIORegister(regnum);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Write IO registers
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_WriteIOReg:
        {
            // get register offset
            uint8 regnum = 0;
            switch (destination->mode)
            {
            case Instruction::AddressMode_Imm8:
                regnum = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                regnum = m_registers.reg8[destination->reg8];
                break;

            default:
                UnreachableCode();
            }

            // write memory
            DebugAssert(source->mode == Instruction::AddressMode_Reg8);
            m_system->CPUWriteIORegister(regnum, m_registers.reg8[source->reg8]);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Increment
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Increment:
        {
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                {
                    // 8-bit register increment
                    uint8 old_value = m_registers.reg8[destination->reg8];
                    uint8 value = old_value + 1;
                    m_registers.SetFlagZ(value == 0);
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((old_value & 0xF) == 0xF);
                    m_registers.reg8[destination->reg8] = value;
                    break;
                }

            case Instruction::AddressMode_Mem16:
                {
                    // 8-bit memory increment
                    uint16 address = m_registers.reg16[destination->reg16];
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
                    m_registers.reg16[destination->reg16]++;
                    break;
                }
            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Decrement
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Decrement:
        {
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                {
                    // 8-bit register decrement
                    uint8 value = --m_registers.reg8[destination->reg8];
                    m_registers.SetFlagZ(value == 0);
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((value & 0xF) == 0xF);
                    break;
                }

            case Instruction::AddressMode_Mem16:
                {
                    // 8-bit memory decrement
                    uint16 address = m_registers.reg16[destination->reg16];
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
                    m_registers.reg16[destination->reg16]--;
                    break;
                }
            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Add
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Add:
        {
            switch (destination->mode)
            {
                // 8-bit add
            case Instruction::AddressMode_Reg8:
                {
                    // get value to add
                    uint32 addend = 0;
                    switch (source->mode)
                    {
                    case Instruction::AddressMode_Imm8:
                        addend = get_imm8();
                        break;

                    case Instruction::AddressMode_Reg8:
                        addend = m_registers.reg8[source->reg8];
                        break;

                    case Instruction::AddressMode_Mem16:
                        addend = MemReadByte(m_registers.reg16[source->reg16]);
                        break;
                    }

                    // handle carry
                    if (instruction->carry == Instruction::CarryAction_With)
                        addend += (uint32)m_registers.GetFlagC();

                    // store value - only writes to A
                    DebugAssert(destination->reg8 == Reg8_A);
                    uint8 old_value = m_registers.reg8[destination->reg8];
                    uint32 new_value = old_value + addend;
                    m_registers.reg8[destination->reg8] = (uint8)(new_value & 0xFF);
                    m_registers.SetFlagZ(((new_value & 0xFF) == 0));
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH(((new_value & 0xF) + (old_value & 0xF)) > 0xF);
                    m_registers.SetFlagC(new_value > 0xFF);
                    break;
                }

                // 16-bit add
            case Instruction::AddressMode_Reg16:
                {
                    uint16 old_value = m_registers.reg16[destination->reg16];
                    uint32 new_value;

                    // one instruction: ADD SP, r8
                    if (source->mode == Instruction::AddressMode_Imm8)
                    {
                        // can be signed
                        int8 r8 = (int8)get_imm8();
                        if (r8 < 0)
                            new_value = old_value - (uint16)(-r8);
                        else
                            new_value = old_value + (uint16)r8;

                        // clears zero flag for some reason (but reg+reg doesn't)
                        m_registers.SetFlagZ(false);
                    }
                    else
                    {
                        // only goes register+register->register
                        DebugAssert(source->mode == Instruction::AddressMode_Reg16);
                        uint16 addend = m_registers.reg16[source->reg16];
                        new_value = old_value + (uint32)addend;
                    }

                    m_registers.reg16[destination->reg16] = new_value & 0xFFFF;
                    m_registers.SetFlagN(false);
                    m_registers.SetFlagH((new_value & 0xFFF) < ((uint32)old_value & 0xFFF));
                    m_registers.SetFlagC(new_value > 0xFFFF); // correct?
                    break;
                }

            default:
                UnreachableCode();
            }

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Subtract
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Sub:
        {
            // get value to subtract
            uint8 addend = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Imm8:
                addend = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                addend = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                addend = MemReadByte(m_registers.reg16[source->reg16]);
                break;
            }

            // handle carry
            uint8 carry_in = (uint8)(instruction->carry == Instruction::CarryAction_With && m_registers.GetFlagC());

            // store value - only writes to A
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8 && destination->reg8 == Reg8_A);
            uint8 old_value = m_registers.reg8[destination->reg8];
            uint8 new_value = m_registers.reg8[destination->reg8] = old_value - addend - carry_in;
            m_registers.SetFlagZ((new_value == 0));
            m_registers.SetFlagN(true);
            m_registers.SetFlagH((new_value & 0xF) < (old_value & 0xF));
            m_registers.SetFlagC((addend + carry_in) > old_value);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // And
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_And:
        {
            // get value
            uint8 value = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[source->reg16]);
                break;

            case Instruction::AddressMode_Imm8:
                value = get_imm8();
                break;

            default:
                UnreachableCode();
            }

            // AND accumulator with value
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8 && destination->reg8 == Reg8_A);
            value = m_registers.reg8[destination->reg8] &= value;
            
            // update flags
            m_registers.SetFlagZ((value == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(true);
            m_registers.SetFlagC(false);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Or
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_Or:
        {
            // get value
            uint8 value = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[source->reg16]);
                break;

            case Instruction::AddressMode_Imm8:
                value = get_imm8();
                break;

            default:
                UnreachableCode();
            }

            // OR accumulator with value
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8 && destination->reg8 == Reg8_A);
            value = m_registers.reg8[destination->reg8] |= value;
            
            // update flags
            m_registers.SetFlagZ((value == 0));
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(false);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Xor
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_Xor:
        {
            // get value
            uint8 value = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Reg8:
                value = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                value = MemReadByte(m_registers.reg16[source->reg16]);
                break;

            case Instruction::AddressMode_Imm8:
                value = get_imm8();
                break;

            default:
                UnreachableCode();
            }

            // XOR accumulator with value
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8 && destination->reg8 == Reg8_A);
            value = m_registers.reg8[destination->reg8] ^= value;
            m_registers.SetFlagZ(value == 0);
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(false);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Rotate
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Rotate:
        {
            // read value
            uint8 carry_in = (uint8)m_registers.GetFlagC();
            uint8 old_value = 0;
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                old_value = m_registers.reg8[destination->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                old_value = MemReadByte(m_registers.reg16[destination->reg16]);
                break;

            default:
                UnreachableCode();
            }

            // get output value
            uint8 carry_out = carry_in;
            uint8 new_value = old_value;

            // kinda counter-intuitive - but RL = 9-bit rotation, RLC = 8-bit rotation
            // we do a 9-bit rotation anyway, so just replace the first bit (the old carry bit)
            Instruction::RotateDirection dir = source->direction;
            if (dir == Instruction::RotateDirection_Left)
            {
                carry_out = (old_value >> 7);
                if (instruction->carry == Instruction::CarryAction_With)
                    carry_in = carry_out;

                new_value = (old_value << 1) | (carry_in);
            }
            else
            {
                carry_out = (old_value & 1);
                if (instruction->carry == Instruction::CarryAction_With)
                    carry_in = carry_out;

                new_value = (old_value >> 1) | (carry_in << 7);
            }

            // set new values
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] = new_value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[destination->reg16], new_value);
                break;

            default:
                UnreachableCode();
            }

            // update flags
            m_registers.SetFlagZ(false);
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(carry_out != 0);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // CPL ie Not
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_Not:
        {
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8 && destination->reg8 == Reg8_A);
            m_registers.reg8[destination->reg8] = ~m_registers.reg8[source->reg8];
            m_registers.SetFlagN(true);
            m_registers.SetFlagH(true);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Swap high/low nibbles
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_Swap:
        {
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

            // swap nibbles
            value = (value << 4) | (value >> 4);

            // write value
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[destination->reg16], value);
                break;

            default:
                UnreachableCode();
                break;
            }
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Compare
        //////////////////////////////////////////////////////////////////////////

    case Instruction::Type_Cmp:
        {
            // get lhs
            DebugAssert(destination->mode == Instruction::AddressMode_Reg8 && destination->reg8 == Reg8_A);
            uint8 lhs = m_registers.reg8[destination->reg8];

            // get rhs
            uint8 rhs = 0;
            switch (source->mode)
            {
            case Instruction::AddressMode_Imm8:
                rhs = get_imm8();
                break;

            case Instruction::AddressMode_Reg8:
                rhs = m_registers.reg8[source->reg8];
                break;

            case Instruction::AddressMode_Mem16:
                rhs = MemReadByte(m_registers.reg16[source->reg16]);
                break;

            default:
                UnreachableCode();
            }

            // implemented in hardware as a subtraction?
            m_registers.SetFlagZ(lhs == rhs);
            m_registers.SetFlagN(true);
            m_registers.SetFlagH((lhs & 0xF) < (rhs & 0xF));
            m_registers.SetFlagC(lhs < rhs);
            break;
        }


        //////////////////////////////////////////////////////////////////////////
        // Test Bit
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_TestBit:
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
    case Instruction::Type_SetBit:
        {
            uint8 mask = (1 << instruction->bitnum);
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] |= mask;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteWord(m_registers.reg16[destination->reg16], MemReadByte(m_registers.reg16[destination->reg16]) | mask);
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
    case Instruction::Type_ResetBit:
        {
            uint8 mask = (1 << instruction->bitnum);
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] &= ~mask;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteWord(m_registers.reg16[destination->reg16], MemReadByte(m_registers.reg16[destination->reg16]) & ~mask);
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

    case Instruction::Type_JumpAbsolute:
        {
            // get jump location
            uint16 address;
            switch (source->mode)
            {
            case Instruction::AddressMode_Imm16:
                address = get_imm16();
                break;

            case Instruction::AddressMode_Reg16:
                address = m_registers.reg16[source->reg16];
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

    case Instruction::Type_JumpRelative:
        {
            if (TestPredicate(instruction->predicate))
            {
                int8 d8 = (int8)get_imm8();
                if (d8 < 0)
                    m_registers.PC -= -d8;
                else
                    m_registers.PC += d8;
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

    case Instruction::Type_Call:
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

    case Instruction::Type_Return:
        {
            if (TestPredicate(instruction->predicate))
                m_registers.PC = PopWord();
            else
                cycles_consumed = instruction->cycles_skipped;

            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Push
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_Push:
        {
            DebugAssert(source->mode == Instruction::AddressMode_Reg16);
            PushWord(m_registers.reg16[source->reg16]);
            break;
        }

        //////////////////////////////////////////////////////////////////////////
        // Pop
        //////////////////////////////////////////////////////////////////////////
    case Instruction::Type_Pop:
        {
            DebugAssert(destination->mode == Instruction::AddressMode_Reg16);
            m_registers.reg16[destination->reg16] = PopWord();
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

        //////////////////////////////////////////////////////////////////////////
        // Untyped instructions
        //////////////////////////////////////////////////////////////////////////
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
            Log_DevPrintf("CPU Halt at %04X", original_pc);
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
            Log_DevPrintf("Interrupts enabled from EI");
            break;
        }

    case Instruction::Type_DI:
        {
            m_registers.IME = false;
            Log_DevPrintf("Interrupts disabled from DI");
            break;
        }

    case Instruction::Type_LDHL_SPR8:
        {
            uint16 load_address = m_registers.SP;
            int8 offset = (int8)get_imm8();
            if (offset < 0)
                load_address -= -offset;
            else
                load_address += offset;

            // load to HL
            m_registers.HL = MemReadWord(load_address);

            // affects flags, only load that does. how??
            m_registers.SetFlagZ(false);
            m_registers.SetFlagN(false);
            m_registers.SetFlagH(false);
            m_registers.SetFlagC(false);
            break;
        }

        // shift left arithmetic
    case Instruction::Type_SLA:
        {
            uint8 value;
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
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[source->reg16], value);
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
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[source->reg16], value);
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
            switch (destination->mode)
            {
            case Instruction::AddressMode_Reg8:
                m_registers.reg8[destination->reg8] = value;
                break;

            case Instruction::AddressMode_Mem16:
                MemWriteByte(m_registers.reg16[source->reg16], value);
                break;

            default:
                UnreachableCode();
            }

            break;
        }

    default:
        UnreachableCode();
        break;

    }

    #undef get_imm8
    #undef get_imm16
    return cycles_consumed;
}

