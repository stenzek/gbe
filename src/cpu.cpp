#include "cpu.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/String.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryWriter.h"
#include "YBaseLib/Error.h"
Log_SetChannel(CPU);

CPU::CPU(System *system)
    : m_system(system)
{

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
    m_disabled = false;
}

void CPU::Push(uint8 value)
{
    //DebugAssert(m_registers.SP > 0);
    m_registers.SP--;
    m_system->CPUWrite(m_registers.SP, value);
}

void CPU::PushWord(uint16 value)
{
    //DebugAssert(m_registers.SP >= 0xC002);
    //m_registers.SP--;
    //m_mmu->Write8(m_registers.SP, (value >> 8) & 0xFF);
    //m_registers.SP--;
    //m_mmu->Write8(m_registers.SP, value & 0xFF);
    m_registers.SP -= 2;
    MemWriteWord(m_registers.SP, value);
}

uint8 CPU::PopByte()
{
    //DebugAssert(m_registers.SP < 0xFFFF);
    uint8 value = MemReadByte(m_registers.SP);
    m_registers.SP++;
    return value;
}

uint16 CPU::PopWord()
{
    //DebugAssert(m_registers.SP < 0xFFFE);

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

void CPU::Disable(bool disabled)
{
    TRACE("CPU disable: %s", (disabled) ? "enabled" : "disabled");
    m_disabled = disabled;
}

bool CPU::LoadState(ByteStream *pStream, BinaryReader &binaryReader, Error *pError)
{
    // Read registers
    m_registers.F = binaryReader.ReadUInt8();
    m_registers.A = binaryReader.ReadUInt8();
    m_registers.C = binaryReader.ReadUInt8();
    m_registers.B = binaryReader.ReadUInt8();
    m_registers.E = binaryReader.ReadUInt8();
    m_registers.D = binaryReader.ReadUInt8();
    m_registers.L = binaryReader.ReadUInt8();
    m_registers.H = binaryReader.ReadUInt8();
    m_registers.SP = binaryReader.ReadUInt16();
    m_registers.PC = binaryReader.ReadUInt16();
    m_registers.IME = binaryReader.ReadBool();
    m_registers.IE = binaryReader.ReadUInt8();
    m_registers.IF = binaryReader.ReadUInt8();
    m_clock = binaryReader.ReadUInt32();
    m_halted = binaryReader.ReadBool();
    m_disabled = binaryReader.ReadBool();
    return true;
}

void CPU::SaveState(ByteStream *pStream, BinaryWriter &binaryWriter)
{
    // Write registers
    binaryWriter.WriteUInt8(m_registers.F);
    binaryWriter.WriteUInt8(m_registers.A);
    binaryWriter.WriteUInt8(m_registers.C);
    binaryWriter.WriteUInt8(m_registers.B);
    binaryWriter.WriteUInt8(m_registers.E);
    binaryWriter.WriteUInt8(m_registers.D);
    binaryWriter.WriteUInt8(m_registers.L);
    binaryWriter.WriteUInt8(m_registers.H);
    binaryWriter.WriteUInt16(m_registers.SP);
    binaryWriter.WriteUInt16(m_registers.PC);
    binaryWriter.WriteBool(m_registers.IME);
    binaryWriter.WriteUInt8(m_registers.IE);
    binaryWriter.WriteUInt8(m_registers.IF);
    binaryWriter.WriteUInt32(m_clock);
    binaryWriter.WriteBool(m_halted);
    binaryWriter.WriteBool(m_disabled);
}
uint8 CPU::ReadOperandByte()
{
    return MemReadByte(m_registers.PC++);
}

uint16 CPU::ReadOperandWord()
{
    uint8 low = MemReadByte(m_registers.PC++);
    uint8 high = MemReadByte(m_registers.PC++);
    return (uint16(high) << 8) | low;
}

int8 CPU::ReadOperandSignedByte()
{
    return (int8)MemReadByte(m_registers.PC++);
}

uint8 CPU::INSTR_inc(uint8 value)
{
    // 8-bit register increment
    uint8 old_value = value++;
    m_registers.SetFlagZ(value == 0);
    m_registers.SetFlagN(false);
    m_registers.SetFlagH((old_value & 0xF) == 0xF);
    return value;
}

uint8 CPU::INSTR_dec(uint8 value)
{
    // 8-bit register decrement
    value--;
    m_registers.SetFlagZ(value == 0);
    m_registers.SetFlagN(true);
    m_registers.SetFlagH((value & 0xF) == 0xF);
    return value;
}

void CPU::INSTR_add(uint8 value)
{
    // store value - only writes to A
    uint8 old_value = m_registers.A;
    uint32 addend = (uint32)value;
    uint32 new_value = old_value + addend;
    m_registers.A = (uint8)(new_value & 0xFF);
    m_registers.SetFlagZ(((new_value & 0xFF) == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(((old_value & 0xF) + (addend & 0xF)) > 0xF);
    m_registers.SetFlagC(new_value > 0xFF);
}

void CPU::INSTR_adc(uint8 value)
{
    // handle carry
    uint8 carry_in = (uint8)m_registers.GetFlagC();

    // do operation
    uint8 old_value = m_registers.A;
    uint8 new_value = old_value + value + carry_in;
    m_registers.A = new_value;

    // update flags
    m_registers.SetFlagZ((new_value == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(((old_value & 0xF) + (value & 0xF) + carry_in) > 0xF);
    m_registers.SetFlagC(((uint32)old_value + value + carry_in) > 0xFF);
}

void CPU::INSTR_sub(uint8 value)
{
    // store value - only writes to A
    uint8 old_value = m_registers.A;
    uint8 new_value = old_value - value;
    m_registers.A = new_value;

    m_registers.SetFlagZ((new_value == 0));
    m_registers.SetFlagN(true);
    m_registers.SetFlagH((new_value & 0xF) > (old_value & 0xF));
    m_registers.SetFlagC(value > old_value);
}

void CPU::INSTR_sbc(uint8 value)
{
    // handle carry
    uint8 carry_in = (uint8)m_registers.GetFlagC();
    uint8 old_value = m_registers.A;
    uint8 new_value = old_value - value - carry_in;
    m_registers.A = new_value;

    // update flags
    m_registers.SetFlagZ((new_value == 0));
    m_registers.SetFlagN(true);
    m_registers.SetFlagH(((value & 0xF) + carry_in) > (old_value & 0xF));
    m_registers.SetFlagC(((uint32)value + carry_in) > old_value);
}

void CPU::INSTR_and(uint8 value)
{
    // AND accumulator with value
    m_registers.A &= value;

    // update flags
    m_registers.SetFlagZ((m_registers.A == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(true);
    m_registers.SetFlagC(false);
}

void CPU::INSTR_or(uint8 value)
{
    // OR accumulator with value
    m_registers.A |= value;

    // update flags
    m_registers.SetFlagZ((m_registers.A == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(false);
    m_registers.SetFlagC(false);
}

void CPU::INSTR_xor(uint8 value)
{
    // XOR accumulator with value
    m_registers.A ^= value;
    m_registers.SetFlagZ((m_registers.A == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(false);
    m_registers.SetFlagC(false);
}

void CPU::INSTR_cp(uint8 value)
{
    // implemented in hardware as a subtraction?
    m_registers.SetFlagZ(m_registers.A == value);
    m_registers.SetFlagN(true);
    m_registers.SetFlagH((m_registers.A & 0xF) < (value & 0xF));
    m_registers.SetFlagC(m_registers.A < value);
}

uint8 CPU::INSTR_rl(uint8 value, bool set_z)
{
    // 9-bit rotation, carry -> bit 0
    uint8 old_value = value;
    value = (value << 1) | (uint8)m_registers.GetFlagC();

    // update flags
    m_registers.SetFlagC(!!(old_value & 0x80));
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);

    // non-prefixed rotates zero z flag
    if (set_z)
        m_registers.SetFlagZ((value == 0));
    else
        m_registers.SetFlagZ(false);

    return value;
}

uint8 CPU::INSTR_rr(uint8 value, bool set_z)
{
    // 9-bit rotation, carry -> bit 7
    uint8 old_value = value;
    value = (value >> 1) | ((uint8)m_registers.GetFlagC() << 7);

    // update flags
    m_registers.SetFlagC(!!(old_value & 0x01));
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);

    // non-prefixed rotates zero z flag
    if (set_z)
        m_registers.SetFlagZ((value == 0));
    else
        m_registers.SetFlagZ(false);

    return value;
}

uint8 CPU::INSTR_rlc(uint8 value, bool set_z)
{
    // bit 7 -> carry
    m_registers.SetFlagC((value & 0x80) != 0);

    // rotate to left
    value = ((value & 0x80) >> 7) | (value << 1);

    // update flags
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);

    // non-prefixed rotates zero z flag
    if (set_z)
        m_registers.SetFlagZ((value == 0));
    else
        m_registers.SetFlagZ(false);

    return value;
}

uint8 CPU::INSTR_rrc(uint8 value, bool set_z)
{
    // bit 0 -> carry
    m_registers.SetFlagC((value & 0x01) != 0);

    // rotate to right
    value = ((value & 0x01) << 7) | (value >> 1);

    // update flags
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);

    // non-prefixed rotates zero z flag
    if (set_z)
        m_registers.SetFlagZ((value == 0));
    else
        m_registers.SetFlagZ(false);

    return value;
}

uint8 CPU::INSTR_sla(uint8 value)
{
    // shift to left, bit 7 -> carry, bit 0 <- 0
    m_registers.SetFlagC((value & 0x80) != 0);
    value <<= 1;

    // update flags
    m_registers.SetFlagZ((value == 0));
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);
    return value;
}

uint8 CPU::INSTR_sra(uint8 value)
{
    // shift to right, keep bit 7, bit 1 -> carry
    m_registers.SetFlagC((value & 0x01) != 0);
    value = (value & 0x80) | (value >> 1);

    // update flags
    m_registers.SetFlagZ((value == 0));
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);
    return value;
}

uint8 CPU::INSTR_srl(uint8 value)
{
    // shift to right, bit 7 <- 0, bit 0 -> carry
    m_registers.SetFlagC((value & 0x01));
    value >>= 1;

    // update flags
    m_registers.SetFlagZ((value == 0));
    m_registers.SetFlagH(false);
    m_registers.SetFlagN(false);
    return value;
}

uint8 CPU::INSTR_swap(uint8 value)
{
    // swap nibbles
    value = (value << 4) | (value >> 4);
    m_registers.SetFlagZ((value == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(false);
    m_registers.SetFlagC(false);
    return value;
}

void CPU::INSTR_bit(uint8 bit, uint8 value)
{
    uint8 mask = uint8(1 << bit);
    value &= mask;
    m_registers.SetFlagZ((value == 0));
    m_registers.SetFlagN(false);
    m_registers.SetFlagH(true);
}

uint8 CPU::INSTR_res(uint8 bit, uint8 value)
{
    uint8 mask = ~uint8(1 << bit);
    return value & mask;
}

uint8 CPU::INSTR_set(uint8 bit, uint8 value)
{
    uint8 mask = uint8(1 << bit);
    return value | mask;
}

void CPU::INSTR_addhl(uint16 value)
{
    uint16 old_value = m_registers.HL;
    uint32 new_value = old_value + (uint32)value;
    m_registers.HL = new_value & 0xFFFF;
    m_registers.SetFlagN(false);
    m_registers.SetFlagH((new_value & 0xFFF) < ((uint32)old_value & 0xFFF));
    m_registers.SetFlagC(new_value > 0xFFFF); // correct?
}

void CPU::INSTR_addsp(int8 displacement)
{
    // Add 8-bit signed value to R16
    uint16 old_value = m_registers.SP;
    uint16 new_value = m_registers.SP;

    // can be signed
    if (displacement < 0)
        new_value -= uint8(-displacement);
    else
        new_value += uint8(displacement);

    DelayCycle();

    // clears zero flag for some reason (but reg+reg doesn't)
    m_registers.SP = new_value;
    m_registers.SetFlagZ(false);
    m_registers.SetFlagN(false);
    m_registers.SetFlagH((new_value & 0xF) < (old_value & 0xF));
    m_registers.SetFlagC((new_value & 0xFF) < (old_value & 0xFF));

    DelayCycle();
}

void CPU::INSTR_ldhlsp(int8 displacement)
{
    uint16 value = m_registers.SP;
    uint16 old_value = value;
    if (displacement < 0)
        value -= uint8(-displacement);
    else
        value += uint8(displacement);

    m_registers.HL = value;
    DelayCycle();

    // affects flags, only load that does. how??
    m_registers.SetFlagZ(false);
    m_registers.SetFlagN(false);
    m_registers.SetFlagH((value & 0xF) < (old_value & 0xF));
    m_registers.SetFlagC((value & 0xFF) < (old_value & 0xFF));
}

void CPU::INSTR_halt()
{
    m_halted = true;
}

void CPU::INSTR_stop()
{
    // skip the parameter (todo implement bug here)
    m_registers.PC++;
    if (!m_system->InCGBMode() || !m_system->SwitchCGBSpeed())
        m_halted = true;
}

void CPU::INSTR_jr(int8 displacement)
{
    if (displacement < 0)
        m_registers.PC -= uint8(-displacement);
    else
        m_registers.PC += uint8(displacement);

    DelayCycle();
}

void CPU::INSTR_jp(uint16 address)
{
    m_registers.PC = address;
    DelayCycle();
}

void CPU::INSTR_call(uint16 address)
{
    PushWord(m_registers.PC);
    m_registers.PC = address;
    DelayCycle();
}

void CPU::INSTR_ret()
{
    m_registers.PC = PopWord();
    DelayCycle();
}

void CPU::INSTR_rst(uint8 vector)
{
    PushWord(m_registers.PC);
    m_registers.PC = (uint16)vector;
    DelayCycle();
}

void CPU::INSTR_daa()
{
    uint16 value = uint16(m_registers.A);
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
}

void CPU::ExecuteInstruction()
{
    // cpu disabled for memory transfer?
    if (m_disabled)
    {
        m_system->AddCPUCycles(4);
        return;
    }

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

                    TRACE("Entering interrupt handler $%04X, PC was $%04X", jump_locations[i], m_registers.PC);
                    //DisassembleFrom(m_system, m_registers.PC, 10);

                    PushWord(m_registers.PC);
                    m_registers.PC = jump_locations[i];
                    m_halted = false;

                    // interrupt takes 20 cycles total, 2 memory writes
                    m_system->AddCPUCycles(20 - 4 - 4);
                    return;
                }
            }
        }
    }

    // if halted, simulate a single cycle to keep the display/audio going
    if (m_halted)
    {
        m_system->AddCPUCycles(4);
        return;
    }

#ifdef Y_BUILD_CONFIG_DEBUG
    // debug
    static bool disasm_enabled = false;
    //static bool disasm_enabled = true;
    if (disasm_enabled)
    {
        SmallString disasm;
        if (Disassemble(&disasm, m_system, m_registers.PC))
            Log_DevPrintf("exec: [AF:%04X,BC:%04X,DE:%04X,HL:%04X] %s", m_registers.AF, m_registers.BC, m_registers.DE, m_registers.HL, disasm.GetCharArray());
        else
            Log_DevPrintf("disasm fail at %04X", m_registers.PC);
    }
#endif

    // fetch
    uint8 opcode = MemReadByte(m_registers.PC++);

    // temporaries
    uint16 dstaddr;
    uint8 displacement;
    uint8 ioreg;
    switch (opcode)
    {
    case 0x00:                                                                                                              break;  // NOP
    case 0x01:  m_registers.BC = ReadOperandWord();                                                                         break;  // LD BC, d16
    case 0x02:  MemWriteByte(m_registers.BC, m_registers.A);                                                                break;  // LD (BC), A
    case 0x03:  m_registers.BC++; DelayCycle();                                                                             break;  // INC BC
    case 0x04:  m_registers.B = INSTR_inc(m_registers.B);                                                                   break;  // INC B
    case 0x05:  m_registers.B = INSTR_dec(m_registers.B);                                                                   break;  // DEC B
    case 0x06:  m_registers.B = ReadOperandByte();                                                                          break;  // LD B, d8
    case 0x07:  m_registers.A = INSTR_rlc(m_registers.A, false);                                                            break;  // RLCA
    case 0x08:  MemWriteWord(ReadOperandWord(), m_registers.SP);                                                            break;  // LD (a16), SP
    case 0x09:  INSTR_addhl(m_registers.BC);                                                                                break;  // ADD HL, BC
    case 0x0A:  m_registers.A = MemReadByte(m_registers.BC);                                                                break;  // LD A, (BC)
    case 0x0B:  m_registers.BC--; DelayCycle();                                                                             break;  // DEC BC
    case 0x0C:  m_registers.C = INSTR_inc(m_registers.C);                                                                   break;  // INC C
    case 0x0D:  m_registers.C = INSTR_dec(m_registers.C);                                                                   break;  // DEC C
    case 0x0E:  m_registers.C = ReadOperandByte();                                                                          break;  // LD C, d8
    case 0x0F:  m_registers.A = INSTR_rrc(m_registers.A, false);                                                            break;  // RRCA
    case 0x10:  INSTR_stop();                                                                                               break;  // STOP 0
    case 0x11:  m_registers.DE = ReadOperandWord();                                                                         break;  // LD DE, d16
    case 0x12:  MemWriteByte(m_registers.DE, m_registers.A);                                                                break;  // LD (DE), A
    case 0x13:  m_registers.DE++; DelayCycle();                                                                             break;  // INC DE
    case 0x14:  m_registers.D = INSTR_inc(m_registers.D);                                                                   break;  // INC D
    case 0x15:  m_registers.D = INSTR_dec(m_registers.D);                                                                   break;  // DEC D
    case 0x16:  m_registers.D = ReadOperandByte();                                                                          break;  // LD D, d8
    case 0x17:  m_registers.A = INSTR_rl(m_registers.A, false);                                                             break;  // RLA
    case 0x18:  displacement = ReadOperandByte(); INSTR_jr(displacement);                                                   break;  // JR r8
    case 0x19:  INSTR_addhl(m_registers.DE);                                                                                break;  // ADD HL, DE
    case 0x1A:  m_registers.A = MemReadByte(m_registers.DE);                                                                break;  // LD A, (DE)
    case 0x1B:  m_registers.DE--; DelayCycle();                                                                             break;  // DEC DE
    case 0x1C:  m_registers.E = INSTR_inc(m_registers.E);                                                                   break;  // INC E
    case 0x1D:  m_registers.E = INSTR_dec(m_registers.E);                                                                   break;  // DEC E
    case 0x1E:  m_registers.E = ReadOperandByte();                                                                          break;  // LD E, d8
    case 0x1F:  m_registers.A = INSTR_rr(m_registers.A, false);                                                             break;  // RRA
    case 0x20:  displacement = ReadOperandByte(); if (!m_registers.GetFlagZ()) { INSTR_jr(displacement); }                  break;  // JR NZ, r8
    case 0x21:  m_registers.HL = ReadOperandWord();                                                                         break;  // LD HL, d16
    case 0x22:  MemWriteByte(m_registers.HL++, m_registers.A);                                                              break;  // LD (HL+), A
    case 0x23:  m_registers.HL++; DelayCycle();                                                                             break;  // INC HL
    case 0x24:  m_registers.H = INSTR_inc(m_registers.H);                                                                   break;  // INC H
    case 0x25:  m_registers.H = INSTR_dec(m_registers.H);                                                                   break;  // DEC H
    case 0x26:  m_registers.H = ReadOperandByte();                                                                          break;  // LD H, d8
    case 0x27:  INSTR_daa();                                                                                                break;  // DAA
    case 0x28:  displacement = ReadOperandByte(); if (m_registers.GetFlagZ()) { INSTR_jr(displacement); }                   break;  // JR Z, r8
    case 0x29:  INSTR_addhl(m_registers.HL);                                                                                break;  // ADD HL, HL
    case 0x2A:  m_registers.A = MemReadByte(m_registers.HL++);                                                              break;  // LD A, (HL+)
    case 0x2B:  m_registers.HL--; DelayCycle();                                                                             break;  // DEC HL
    case 0x2C:  m_registers.L = INSTR_inc(m_registers.L);                                                                   break;  // INC L
    case 0x2D:  m_registers.L = INSTR_dec(m_registers.L);                                                                   break;  // DEC L
    case 0x2E:  m_registers.L = ReadOperandByte();                                                                          break;  // LD L, d8
    case 0x2F:  m_registers.A = ~m_registers.A; m_registers.SetFlagN(true); m_registers.SetFlagH(true);                     break;  // CPL
    case 0x30:  displacement = ReadOperandByte(); if (!m_registers.GetFlagC()) { INSTR_jr(displacement); }                  break;  // JR NC, r8
    case 0x31:  m_registers.SP = ReadOperandWord();                                                                         break;  // LD SP, d16
    case 0x32:  MemWriteByte(m_registers.HL--, m_registers.A);                                                              break;  // LD (HL-), A
    case 0x33:  m_registers.SP++; DelayCycle();                                                                             break;  // INC SP
    case 0x34:  MemWriteByte(m_registers.HL, INSTR_inc(MemReadByte(m_registers.HL)));                                       break;  // INC (HL)
    case 0x35:  MemWriteByte(m_registers.HL, INSTR_dec(MemReadByte(m_registers.HL)));                                       break;  // DEC (HL)
    case 0x36:  MemWriteByte(m_registers.HL, ReadOperandByte());                                                            break;  // LD (HL), d8
    case 0x37:  m_registers.SetFlagN(false); m_registers.SetFlagH(false); m_registers.SetFlagC(true);                       break;  // SCF
    case 0x38:  displacement = ReadOperandByte(); if (m_registers.GetFlagC()) { INSTR_jr(displacement); }                   break;  // JR C, r8
    case 0x39:  INSTR_addhl(m_registers.SP);                                                                                break;  // ADD HL, SP
    case 0x3A:  m_registers.A = MemReadByte(m_registers.HL--);                                                              break;  // LD A, (HL-)
    case 0x3B:  m_registers.SP--; DelayCycle();                                                                             break;  // DEC SP
    case 0x3C:  m_registers.A = INSTR_inc(m_registers.A);                                                                   break;  // INC A
    case 0x3D:  m_registers.A = INSTR_dec(m_registers.A);                                                                   break;  // DEC A
    case 0x3E:  m_registers.A = ReadOperandByte();                                                                          break;  // LD A, d8
    case 0x3F:  m_registers.SetFlagN(false); m_registers.SetFlagH(false); m_registers.SetFlagC(!m_registers.GetFlagC());    break;  // CCF
    case 0x40:  m_registers.B = m_registers.B;                                                                              break;  // LD B, B
    case 0x41:  m_registers.B = m_registers.C;                                                                              break;  // LD B, C
    case 0x42:  m_registers.B = m_registers.D;                                                                              break;  // LD B, D
    case 0x43:  m_registers.B = m_registers.E;                                                                              break;  // LD B, E
    case 0x44:  m_registers.B = m_registers.H;                                                                              break;  // LD B, H
    case 0x45:  m_registers.B = m_registers.L;                                                                              break;  // LD B, L
    case 0x46:  m_registers.B = MemReadByte(m_registers.HL);                                                                break;  // LD B, (HL)
    case 0x47:  m_registers.B = m_registers.A;                                                                              break;  // LD B, A
    case 0x48:  m_registers.C = m_registers.B;                                                                              break;  // LD C, B
    case 0x49:  m_registers.C = m_registers.C;                                                                              break;  // LD C, C
    case 0x4A:  m_registers.C = m_registers.D;                                                                              break;  // LD C, D
    case 0x4B:  m_registers.C = m_registers.E;                                                                              break;  // LD C, E
    case 0x4C:  m_registers.C = m_registers.H;                                                                              break;  // LD C, H
    case 0x4D:  m_registers.C = m_registers.L;                                                                              break;  // LD C, L
    case 0x4E:  m_registers.C = MemReadByte(m_registers.HL);                                                                break;  // LD C, (HL)
    case 0x4F:  m_registers.C = m_registers.A;                                                                              break;  // LD C, A
    case 0x50:  m_registers.D = m_registers.B;                                                                              break;  // LD D, B
    case 0x51:  m_registers.D = m_registers.C;                                                                              break;  // LD D, C
    case 0x52:  m_registers.D = m_registers.D;                                                                              break;  // LD D, D
    case 0x53:  m_registers.D = m_registers.E;                                                                              break;  // LD D, E
    case 0x54:  m_registers.D = m_registers.H;                                                                              break;  // LD D, H
    case 0x55:  m_registers.D = m_registers.L;                                                                              break;  // LD D, L
    case 0x56:  m_registers.D = MemReadByte(m_registers.HL);                                                                break;  // LD D, (HL)
    case 0x57:  m_registers.D = m_registers.A;                                                                              break;  // LD D, A
    case 0x58:  m_registers.E = m_registers.B;                                                                              break;  // LD E, B
    case 0x59:  m_registers.E = m_registers.C;                                                                              break;  // LD E, C
    case 0x5A:  m_registers.E = m_registers.D;                                                                              break;  // LD E, D
    case 0x5B:  m_registers.E = m_registers.E;                                                                              break;  // LD E, E
    case 0x5C:  m_registers.E = m_registers.H;                                                                              break;  // LD E, H
    case 0x5D:  m_registers.E = m_registers.L;                                                                              break;  // LD E, L
    case 0x5E:  m_registers.E = MemReadByte(m_registers.HL);                                                                break;  // LD E, (HL)
    case 0x5F:  m_registers.E = m_registers.A;                                                                              break;  // LD E, A
    case 0x60:  m_registers.H = m_registers.B;                                                                              break;  // LD H, B
    case 0x61:  m_registers.H = m_registers.C;                                                                              break;  // LD H, C
    case 0x62:  m_registers.H = m_registers.D;                                                                              break;  // LD H, D
    case 0x63:  m_registers.H = m_registers.E;                                                                              break;  // LD H, E
    case 0x64:  m_registers.H = m_registers.H;                                                                              break;  // LD H, H
    case 0x65:  m_registers.H = m_registers.L;                                                                              break;  // LD H, L
    case 0x66:  m_registers.H = MemReadByte(m_registers.HL);                                                                break;  // LD H, (HL)
    case 0x67:  m_registers.H = m_registers.A;                                                                              break;  // LD H, A
    case 0x68:  m_registers.L = m_registers.B;                                                                              break;  // LD L, B
    case 0x69:  m_registers.L = m_registers.C;                                                                              break;  // LD L, C
    case 0x6A:  m_registers.L = m_registers.D;                                                                              break;  // LD L, D
    case 0x6B:  m_registers.L = m_registers.E;                                                                              break;  // LD L, E
    case 0x6C:  m_registers.L = m_registers.H;                                                                              break;  // LD L, H
    case 0x6D:  m_registers.L = m_registers.L;                                                                              break;  // LD L, L
    case 0x6E:  m_registers.L = MemReadByte(m_registers.HL);                                                                break;  // LD L, (HL)
    case 0x6F:  m_registers.L = m_registers.A;                                                                              break;  // LD L, A
    case 0x70:  MemWriteByte(m_registers.HL, m_registers.B);                                                                break;  // LD (HL), B
    case 0x71:  MemWriteByte(m_registers.HL, m_registers.C);                                                                break;  // LD (HL), C
    case 0x72:  MemWriteByte(m_registers.HL, m_registers.D);                                                                break;  // LD (HL), D
    case 0x73:  MemWriteByte(m_registers.HL, m_registers.E);                                                                break;  // LD (HL), E
    case 0x74:  MemWriteByte(m_registers.HL, m_registers.H);                                                                break;  // LD (HL), H
    case 0x75:  MemWriteByte(m_registers.HL, m_registers.L);                                                                break;  // LD (HL), L
    case 0x76:  INSTR_halt();                                                                                               break;  // HALT
    case 0x77:  MemWriteByte(m_registers.HL, m_registers.A);                                                                break;  // LD (HL), A
    case 0x78:  m_registers.A = m_registers.B;                                                                              break;  // LD A, B
    case 0x79:  m_registers.A = m_registers.C;                                                                              break;  // LD A, C
    case 0x7A:  m_registers.A = m_registers.D;                                                                              break;  // LD A, D
    case 0x7B:  m_registers.A = m_registers.E;                                                                              break;  // LD A, E
    case 0x7C:  m_registers.A = m_registers.H;                                                                              break;  // LD A, H
    case 0x7D:  m_registers.A = m_registers.L;                                                                              break;  // LD A, L
    case 0x7E:  m_registers.A = MemReadByte(m_registers.HL);                                                                break;  // LD A, (HL)
    case 0x7F:  m_registers.A = m_registers.A;                                                                              break;  // LD A, A
    case 0x80:  INSTR_add(m_registers.B);                                                                                   break;  // ADD A, B
    case 0x81:  INSTR_add(m_registers.C);                                                                                   break;  // ADD A, C
    case 0x82:  INSTR_add(m_registers.D);                                                                                   break;  // ADD A, D
    case 0x83:  INSTR_add(m_registers.E);                                                                                   break;  // ADD A, E
    case 0x84:  INSTR_add(m_registers.H);                                                                                   break;  // ADD A, H
    case 0x85:  INSTR_add(m_registers.L);                                                                                   break;  // ADD A, L
    case 0x86:  INSTR_add(MemReadByte(m_registers.HL));                                                                     break;  // ADD A, (HL)
    case 0x87:  INSTR_add(m_registers.A);                                                                                   break;  // ADD A, A
    case 0x88:  INSTR_adc(m_registers.B);                                                                                   break;  // ADC A, B
    case 0x89:  INSTR_adc(m_registers.C);                                                                                   break;  // ADC A, C
    case 0x8A:  INSTR_adc(m_registers.D);                                                                                   break;  // ADC A, D
    case 0x8B:  INSTR_adc(m_registers.E);                                                                                   break;  // ADC A, E
    case 0x8C:  INSTR_adc(m_registers.H);                                                                                   break;  // ADC A, H
    case 0x8D:  INSTR_adc(m_registers.L);                                                                                   break;  // ADC A, L
    case 0x8E:  INSTR_adc(MemReadByte(m_registers.HL));                                                                     break;  // ADC A, (HL)
    case 0x8F:  INSTR_adc(m_registers.A);                                                                                   break;  // ADC A, A
    case 0x90:  INSTR_sub(m_registers.B);                                                                                   break;  // ADD A, B
    case 0x91:  INSTR_sub(m_registers.C);                                                                                   break;  // ADD A, C
    case 0x92:  INSTR_sub(m_registers.D);                                                                                   break;  // ADD A, D
    case 0x93:  INSTR_sub(m_registers.E);                                                                                   break;  // ADD A, E
    case 0x94:  INSTR_sub(m_registers.H);                                                                                   break;  // ADD A, H
    case 0x95:  INSTR_sub(m_registers.L);                                                                                   break;  // ADD A, L
    case 0x96:  INSTR_sub(MemReadByte(m_registers.HL));                                                                     break;  // ADD A, (HL)
    case 0x97:  INSTR_sub(m_registers.A);                                                                                   break;  // ADD A, A
    case 0x98:  INSTR_sbc(m_registers.B);                                                                                   break;  // ADC A, B
    case 0x99:  INSTR_sbc(m_registers.C);                                                                                   break;  // ADC A, C
    case 0x9A:  INSTR_sbc(m_registers.D);                                                                                   break;  // ADC A, D
    case 0x9B:  INSTR_sbc(m_registers.E);                                                                                   break;  // ADC A, E
    case 0x9C:  INSTR_sbc(m_registers.H);                                                                                   break;  // ADC A, H
    case 0x9D:  INSTR_sbc(m_registers.L);                                                                                   break;  // ADC A, L
    case 0x9E:  INSTR_sbc(MemReadByte(m_registers.HL));                                                                     break;  // ADC A, (HL)
    case 0x9F:  INSTR_sbc(m_registers.A);                                                                                   break;  // ADC A, A
    case 0xA0:  INSTR_and(m_registers.B);                                                                                   break;  // AND B
    case 0xA1:  INSTR_and(m_registers.C);                                                                                   break;  // AND C
    case 0xA2:  INSTR_and(m_registers.D);                                                                                   break;  // AND D
    case 0xA3:  INSTR_and(m_registers.E);                                                                                   break;  // AND E
    case 0xA4:  INSTR_and(m_registers.H);                                                                                   break;  // AND H
    case 0xA5:  INSTR_and(m_registers.L);                                                                                   break;  // AND L
    case 0xA6:  INSTR_and(MemReadByte(m_registers.HL));                                                                     break;  // AND (HL)
    case 0xA7:  INSTR_and(m_registers.A);                                                                                   break;  // AND A
    case 0xA8:  INSTR_xor(m_registers.B);                                                                                   break;  // XOR B
    case 0xA9:  INSTR_xor(m_registers.C);                                                                                   break;  // XOR C
    case 0xAA:  INSTR_xor(m_registers.D);                                                                                   break;  // XOR D
    case 0xAB:  INSTR_xor(m_registers.E);                                                                                   break;  // XOR E
    case 0xAC:  INSTR_xor(m_registers.H);                                                                                   break;  // XOR H
    case 0xAD:  INSTR_xor(m_registers.L);                                                                                   break;  // XOR L
    case 0xAE:  INSTR_xor(MemReadByte(m_registers.HL));                                                                     break;  // XOR (HL)
    case 0xAF:  INSTR_xor(m_registers.A);                                                                                   break;  // XOR A
    case 0xB0:  INSTR_or(m_registers.B);                                                                                    break;  // OR B
    case 0xB1:  INSTR_or(m_registers.C);                                                                                    break;  // OR C
    case 0xB2:  INSTR_or(m_registers.D);                                                                                    break;  // OR D
    case 0xB3:  INSTR_or(m_registers.E);                                                                                    break;  // OR E
    case 0xB4:  INSTR_or(m_registers.H);                                                                                    break;  // OR H
    case 0xB5:  INSTR_or(m_registers.L);                                                                                    break;  // OR L
    case 0xB6:  INSTR_or(MemReadByte(m_registers.HL));                                                                      break;  // OR (HL)
    case 0xB7:  INSTR_or(m_registers.A);                                                                                    break;  // OR A
    case 0xB8:  INSTR_cp(m_registers.B);                                                                                    break;  // CP B
    case 0xB9:  INSTR_cp(m_registers.C);                                                                                    break;  // CP C
    case 0xBA:  INSTR_cp(m_registers.D);                                                                                    break;  // CP D
    case 0xBB:  INSTR_cp(m_registers.E);                                                                                    break;  // CP E
    case 0xBC:  INSTR_cp(m_registers.H);                                                                                    break;  // CP H
    case 0xBD:  INSTR_cp(m_registers.L);                                                                                    break;  // CP L
    case 0xBE:  INSTR_cp(MemReadByte(m_registers.HL));                                                                      break;  // CP (HL)
    case 0xBF:  INSTR_cp(m_registers.A);                                                                                    break;  // CP A
    case 0xC0:  DelayCycle(); if (!m_registers.GetFlagZ()) { INSTR_ret(); }                                                 break;  // RET NZ
    case 0xC1:  m_registers.BC = PopWord();                                                                                 break;  // POP BC
    case 0xC2:  dstaddr = ReadOperandWord(); if (!m_registers.GetFlagZ()) { INSTR_jp(dstaddr); }                            break;  // JP NZ, a16
    case 0xC3:  dstaddr = ReadOperandWord(); INSTR_jp(dstaddr);                                                             break;  // JP a16
    case 0xC4:  dstaddr = ReadOperandWord(); if (!m_registers.GetFlagZ()) { INSTR_call(dstaddr); }                          break;  // CALL NZ, a16
    case 0xC5:  PushWord(m_registers.BC);                                                                                   break;  // PUSH BC
    case 0xC6:  INSTR_add(ReadOperandByte());                                                                               break;  // ADD a, d8
    case 0xC7:  INSTR_rst(0x00);                                                                                            break;  // RST 00H
    case 0xC8:  DelayCycle(); if (m_registers.GetFlagZ()) { INSTR_ret(); }                                                  break;  // RET Z
    case 0xC9:  INSTR_ret();                                                                                                break;  // RET
    case 0xCA:  dstaddr = ReadOperandWord(); if (m_registers.GetFlagZ()) { INSTR_jp(dstaddr); }                             break;  // JP Z, a16
    case 0xCC:  dstaddr = ReadOperandWord(); if (m_registers.GetFlagZ()) { INSTR_call(dstaddr); }                           break;  // CALL Z, a16
    case 0xCD:  dstaddr = ReadOperandWord(); INSTR_call(dstaddr);                                                           break;  // CALL a16
    case 0xCE:  INSTR_adc(ReadOperandByte());                                                                               break;  // ADC A, d8
    case 0xCF:  INSTR_rst(0x08);                                                                                            break;  // RST 08H
    case 0xD0:  DelayCycle(); if (!m_registers.GetFlagC()) { INSTR_ret(); }                                                 break;  // RET NC
    case 0xD1:  m_registers.DE = PopWord();                                                                                 break;  // POP DE
    case 0xD2:  dstaddr = ReadOperandWord(); if (!m_registers.GetFlagC()) { INSTR_jp(dstaddr); }                            break;  // JP NC, a16
    case 0xD3:  UnreachableCode();                                                                                          break;  // 
    case 0xD4:  dstaddr = ReadOperandWord(); if (!m_registers.GetFlagC()) { INSTR_call(dstaddr); }                          break;  // CALL NC, a16
    case 0xD5:  PushWord(m_registers.DE);                                                                                   break;  // PUSH DE
    case 0xD6:  INSTR_sub(ReadOperandByte());                                                                               break;  // SUB a, d8
    case 0xD7:  INSTR_rst(0x10);                                                                                            break;  // RST 10H
    case 0xD8:  DelayCycle(); if (m_registers.GetFlagC()) { INSTR_ret(); }                                                  break;  // RET C
    case 0xD9:  INSTR_ret(); m_registers.IME = true;                                                                        break;  // RETI
    case 0xDA:  dstaddr = ReadOperandWord(); if (m_registers.GetFlagC()) { INSTR_jp(dstaddr); }                             break;  // JP C, a16
    case 0xDB:  UnreachableCode();                                                                                          break;  // 
    case 0xDC:  dstaddr = ReadOperandWord(); if (m_registers.GetFlagC()) { INSTR_call(dstaddr); }                           break;  // CALL C, a16
    case 0xDD:  UnreachableCode();                                                                                          break;  // 
    case 0xDE:  INSTR_sbc(ReadOperandByte());                                                                               break;  // SBC A, d8
    case 0xDF:  INSTR_rst(0x18);                                                                                            break;  // RST 18H
    case 0xE0:  ioreg = ReadOperandByte(); DelayCycle(); m_system->CPUWriteIORegister(ioreg, m_registers.A);                break;  // LDH (a8), A
    case 0xE1:  m_registers.HL = PopWord();                                                                                 break;  // POP HL
    case 0xE2:  DelayCycle(); m_system->CPUWriteIORegister(m_registers.C, m_registers.A);                                   break;  // LD (C), A
    case 0xE3:  UnreachableCode();                                                                                          break;  // 
    case 0xE4:  UnreachableCode();                                                                                          break;  // 
    case 0xE5:  PushWord(m_registers.HL);                                                                                   break;  // PUSH HL
    case 0xE6:  INSTR_and(ReadOperandByte());                                                                               break;  // AND d8
    case 0xE7:  INSTR_rst(0x20);                                                                                            break;  // RST 20H
    case 0xE8:  INSTR_addsp(ReadOperandSignedByte());                                                                       break;  // ADD SP, r8
    case 0xE9:  m_registers.PC = m_registers.HL;                                                                            break;  // JP (HL)
    case 0xEA:  MemWriteByte(ReadOperandWord(), m_registers.A);                                                             break;  // LD (a16), A
    case 0xEB:  UnreachableCode();                                                                                          break;  // 
    case 0xEC:  UnreachableCode();                                                                                          break;  // 
    case 0xED:  UnreachableCode();                                                                                          break;  // 
    case 0xEE:  INSTR_xor(ReadOperandByte());                                                                               break;  // XOR d8
    case 0xEF:  INSTR_rst(0x28);                                                                                            break;  // RST 28H
    case 0xF0:  ioreg = ReadOperandByte(); m_registers.A = m_system->CPUReadIORegister(ioreg); DelayCycle();                break;  // LDH A, (a8)
    case 0xF1:  m_registers.AF = PopWord() & 0xFFF0;                                                                        break;  // POP AF
    case 0xF2:  m_registers.A = m_system->CPUReadIORegister(m_registers.C); DelayCycle();                                   break;  // LD A, (C)
    case 0xF3:  m_registers.IME = false;                                                                                    break;  // DI
    case 0xF4:  UnreachableCode();                                                                                          break;  // 
    case 0xF5:  PushWord(m_registers.AF);                                                                                   break;  // PUSH AF
    case 0xF6:  INSTR_or(ReadOperandByte());                                                                                break;  // OR d8
    case 0xF7:  INSTR_rst(0x30);                                                                                            break;  // RST 30H
    case 0xF8:  INSTR_ldhlsp(ReadOperandSignedByte());                                                                      break;  // LD HL, SP+r8
    case 0xF9:  m_registers.SP = m_registers.HL; DelayCycle();                                                              break;  // LD SP, HL
    case 0xFA:  m_registers.A = MemReadByte(ReadOperandWord());                                                             break;  // LD A, (a16)
    case 0xFB:  m_registers.IME = true;                                                                                     break;  // EI
    case 0xFC:  UnreachableCode();                                                                                          break;  // 
    case 0xFD:  UnreachableCode();                                                                                          break;  // 
    case 0xFE:  INSTR_cp(ReadOperandByte());                                                                                break;  // CP d8
    case 0xFF:  INSTR_rst(0x38);                                                                                            break;  // RST 38H

        // CB Prefix
    case 0xCB:                                                                                                              // PREFIX CB
        {
            opcode = ReadOperandByte();
            switch (opcode)
            {
            case 0x00:  m_registers.B = INSTR_rlc(m_registers.B, true);                                                     break;  // RLC B
            case 0x01:  m_registers.C = INSTR_rlc(m_registers.C, true);                                                     break;  // RLC C
            case 0x02:  m_registers.D = INSTR_rlc(m_registers.D, true);                                                     break;  // RLC D
            case 0x03:  m_registers.E = INSTR_rlc(m_registers.E, true);                                                     break;  // RLC E
            case 0x04:  m_registers.H = INSTR_rlc(m_registers.H, true);                                                     break;  // RLC H
            case 0x05:  m_registers.L = INSTR_rlc(m_registers.L, true);                                                     break;  // RLC L
            case 0x06:  MemWriteByte(m_registers.HL, INSTR_rlc(MemReadByte(m_registers.HL), true));                         break;  // RLC (HL)
            case 0x07:  m_registers.A = INSTR_rlc(m_registers.A, true);                                                     break;  // RLC A
            case 0x08:  m_registers.B = INSTR_rrc(m_registers.B, true);                                                     break;  // RRC B
            case 0x09:  m_registers.C = INSTR_rrc(m_registers.C, true);                                                     break;  // RRC C
            case 0x0A:  m_registers.D = INSTR_rrc(m_registers.D, true);                                                     break;  // RRC D
            case 0x0B:  m_registers.E = INSTR_rrc(m_registers.E, true);                                                     break;  // RRC E
            case 0x0C:  m_registers.H = INSTR_rrc(m_registers.H, true);                                                     break;  // RRC H
            case 0x0D:  m_registers.L = INSTR_rrc(m_registers.L, true);                                                     break;  // RRC L
            case 0x0E:  MemWriteByte(m_registers.HL, INSTR_rrc(MemReadByte(m_registers.HL), true));                         break;  // RRC (HL)
            case 0x0F:  m_registers.A = INSTR_rrc(m_registers.A, true);                                                     break;  // RRC A
            case 0x10:  m_registers.B = INSTR_rl(m_registers.B, true);                                                      break;  // RL B
            case 0x11:  m_registers.C = INSTR_rl(m_registers.C, true);                                                      break;  // RL C
            case 0x12:  m_registers.D = INSTR_rl(m_registers.D, true);                                                      break;  // RL D
            case 0x13:  m_registers.E = INSTR_rl(m_registers.E, true);                                                      break;  // RL E
            case 0x14:  m_registers.H = INSTR_rl(m_registers.H, true);                                                      break;  // RL H
            case 0x15:  m_registers.L = INSTR_rl(m_registers.L, true);                                                      break;  // RL L
            case 0x16:  MemWriteByte(m_registers.HL, INSTR_rl(MemReadByte(m_registers.HL), true));                          break;  // RL (HL)
            case 0x17:  m_registers.A = INSTR_rl(m_registers.A, true);                                                      break;  // RR A
            case 0x18:  m_registers.B = INSTR_rr(m_registers.B, true);                                                      break;  // RR B
            case 0x19:  m_registers.C = INSTR_rr(m_registers.C, true);                                                      break;  // RR C
            case 0x1A:  m_registers.D = INSTR_rr(m_registers.D, true);                                                      break;  // RR D
            case 0x1B:  m_registers.E = INSTR_rr(m_registers.E, true);                                                      break;  // RR E
            case 0x1C:  m_registers.H = INSTR_rr(m_registers.H, true);                                                      break;  // RR H
            case 0x1D:  m_registers.L = INSTR_rr(m_registers.L, true);                                                      break;  // RR L
            case 0x1E:  MemWriteByte(m_registers.HL, INSTR_rr(MemReadByte(m_registers.HL), true));                          break;  // RR (HL)
            case 0x1F:  m_registers.A = INSTR_rr(m_registers.A, true);                                                      break;  // RR A
            case 0x20:  m_registers.B = INSTR_sla(m_registers.B);                                                           break;  // SLA B
            case 0x21:  m_registers.C = INSTR_sla(m_registers.C);                                                           break;  // SLA C
            case 0x22:  m_registers.D = INSTR_sla(m_registers.D);                                                           break;  // SLA D
            case 0x23:  m_registers.E = INSTR_sla(m_registers.E);                                                           break;  // SLA E
            case 0x24:  m_registers.H = INSTR_sla(m_registers.H);                                                           break;  // SLA H
            case 0x25:  m_registers.L = INSTR_sla(m_registers.L);                                                           break;  // SLA L
            case 0x26:  MemWriteByte(m_registers.HL, INSTR_sla(MemReadByte(m_registers.HL)));                               break;  // SLA (HL)
            case 0x27:  m_registers.A = INSTR_sla(m_registers.A);                                                           break;  // SLA A
            case 0x28:  m_registers.B = INSTR_sra(m_registers.B);                                                           break;  // SRA B
            case 0x29:  m_registers.C = INSTR_sra(m_registers.C);                                                           break;  // SRA C
            case 0x2A:  m_registers.D = INSTR_sra(m_registers.D);                                                           break;  // SRA D
            case 0x2B:  m_registers.E = INSTR_sra(m_registers.E);                                                           break;  // SRA E
            case 0x2C:  m_registers.H = INSTR_sra(m_registers.H);                                                           break;  // SRA H
            case 0x2D:  m_registers.L = INSTR_sra(m_registers.L);                                                           break;  // SRA L
            case 0x2E:  MemWriteByte(m_registers.HL, INSTR_sra(MemReadByte(m_registers.HL)));                               break;  // SRA (HL)
            case 0x2F:  m_registers.A = INSTR_sra(m_registers.A);                                                           break;  // SRA A
            case 0x30:  m_registers.B = INSTR_swap(m_registers.B);                                                          break;  // SWAP B
            case 0x31:  m_registers.C = INSTR_swap(m_registers.C);                                                          break;  // SWAP C
            case 0x32:  m_registers.D = INSTR_swap(m_registers.D);                                                          break;  // SWAP D
            case 0x33:  m_registers.E = INSTR_swap(m_registers.E);                                                          break;  // SWAP E
            case 0x34:  m_registers.H = INSTR_swap(m_registers.H);                                                          break;  // SWAP H
            case 0x35:  m_registers.L = INSTR_swap(m_registers.L);                                                          break;  // SWAP L
            case 0x36:  MemWriteByte(m_registers.HL, INSTR_swap(MemReadByte(m_registers.HL)));                              break;  // SWAP (HL)
            case 0x37:  m_registers.A = INSTR_swap(m_registers.A);                                                          break;  // SWAP A
            case 0x38:  m_registers.B = INSTR_srl(m_registers.B);                                                           break;  // SRL B
            case 0x39:  m_registers.C = INSTR_srl(m_registers.C);                                                           break;  // SRL C
            case 0x3A:  m_registers.D = INSTR_srl(m_registers.D);                                                           break;  // SRL D
            case 0x3B:  m_registers.E = INSTR_srl(m_registers.E);                                                           break;  // SRL E
            case 0x3C:  m_registers.H = INSTR_srl(m_registers.H);                                                           break;  // SRL H
            case 0x3D:  m_registers.L = INSTR_srl(m_registers.L);                                                           break;  // SRL L
            case 0x3E:  MemWriteByte(m_registers.HL, INSTR_srl(MemReadByte(m_registers.HL)));                               break;  // SRL (HL)
            case 0x3F:  m_registers.A = INSTR_srl(m_registers.A);                                                           break;  // SRL A
            case 0x40:  INSTR_bit(0, m_registers.B);                                                                        break;  // BIT 0, B
            case 0x41:  INSTR_bit(0, m_registers.C);                                                                        break;  // BIT 0, C
            case 0x42:  INSTR_bit(0, m_registers.D);                                                                        break;  // BIT 0, D
            case 0x43:  INSTR_bit(0, m_registers.E);                                                                        break;  // BIT 0, E
            case 0x44:  INSTR_bit(0, m_registers.H);                                                                        break;  // BIT 0, H
            case 0x45:  INSTR_bit(0, m_registers.L);                                                                        break;  // BIT 0, L
            case 0x46:  INSTR_bit(0, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 0, (HL)
            case 0x47:  INSTR_bit(0, m_registers.A);                                                                        break;  // BIT 0, A
            case 0x48:  INSTR_bit(1, m_registers.B);                                                                        break;  // BIT 1, B
            case 0x49:  INSTR_bit(1, m_registers.C);                                                                        break;  // BIT 1, C
            case 0x4A:  INSTR_bit(1, m_registers.D);                                                                        break;  // BIT 1, D
            case 0x4B:  INSTR_bit(1, m_registers.E);                                                                        break;  // BIT 1, E
            case 0x4C:  INSTR_bit(1, m_registers.H);                                                                        break;  // BIT 1, H
            case 0x4D:  INSTR_bit(1, m_registers.L);                                                                        break;  // BIT 1, L
            case 0x4E:  INSTR_bit(1, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 1, (HL)
            case 0x4F:  INSTR_bit(1, m_registers.A);                                                                        break;  // BIT 1, A
            case 0x50:  INSTR_bit(2, m_registers.B);                                                                        break;  // BIT 2, B
            case 0x51:  INSTR_bit(2, m_registers.C);                                                                        break;  // BIT 2, C
            case 0x52:  INSTR_bit(2, m_registers.D);                                                                        break;  // BIT 2, D
            case 0x53:  INSTR_bit(2, m_registers.E);                                                                        break;  // BIT 2, E
            case 0x54:  INSTR_bit(2, m_registers.H);                                                                        break;  // BIT 2, H
            case 0x55:  INSTR_bit(2, m_registers.L);                                                                        break;  // BIT 2, L
            case 0x56:  INSTR_bit(2, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 2, (HL)
            case 0x57:  INSTR_bit(2, m_registers.A);                                                                        break;  // BIT 2, A
            case 0x58:  INSTR_bit(3, m_registers.B);                                                                        break;  // BIT 3, B
            case 0x59:  INSTR_bit(3, m_registers.C);                                                                        break;  // BIT 3, C
            case 0x5A:  INSTR_bit(3, m_registers.D);                                                                        break;  // BIT 3, D
            case 0x5B:  INSTR_bit(3, m_registers.E);                                                                        break;  // BIT 3, E
            case 0x5C:  INSTR_bit(3, m_registers.H);                                                                        break;  // BIT 3, H
            case 0x5D:  INSTR_bit(3, m_registers.L);                                                                        break;  // BIT 3, L
            case 0x5E:  INSTR_bit(3, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 3, (HL)
            case 0x5F:  INSTR_bit(3, m_registers.A);                                                                        break;  // BIT 3, A
            case 0x60:  INSTR_bit(4, m_registers.B);                                                                        break;  // BIT 4, B
            case 0x61:  INSTR_bit(4, m_registers.C);                                                                        break;  // BIT 4, C
            case 0x62:  INSTR_bit(4, m_registers.D);                                                                        break;  // BIT 4, D
            case 0x63:  INSTR_bit(4, m_registers.E);                                                                        break;  // BIT 4, E
            case 0x64:  INSTR_bit(4, m_registers.H);                                                                        break;  // BIT 4, H
            case 0x65:  INSTR_bit(4, m_registers.L);                                                                        break;  // BIT 4, L
            case 0x66:  INSTR_bit(4, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 4, (HL)
            case 0x67:  INSTR_bit(4, m_registers.A);                                                                        break;  // BIT 4, A
            case 0x68:  INSTR_bit(5, m_registers.B);                                                                        break;  // BIT 5, B
            case 0x69:  INSTR_bit(5, m_registers.C);                                                                        break;  // BIT 5, C
            case 0x6A:  INSTR_bit(5, m_registers.D);                                                                        break;  // BIT 5, D
            case 0x6B:  INSTR_bit(5, m_registers.E);                                                                        break;  // BIT 5, E
            case 0x6C:  INSTR_bit(5, m_registers.H);                                                                        break;  // BIT 5, H
            case 0x6D:  INSTR_bit(5, m_registers.L);                                                                        break;  // BIT 5, L
            case 0x6E:  INSTR_bit(5, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 5, (HL)
            case 0x6F:  INSTR_bit(5, m_registers.A);                                                                        break;  // BIT 5, A
            case 0x70:  INSTR_bit(6, m_registers.B);                                                                        break;  // BIT 6, B
            case 0x71:  INSTR_bit(6, m_registers.C);                                                                        break;  // BIT 6, C
            case 0x72:  INSTR_bit(6, m_registers.D);                                                                        break;  // BIT 6, D
            case 0x73:  INSTR_bit(6, m_registers.E);                                                                        break;  // BIT 6, E
            case 0x74:  INSTR_bit(6, m_registers.H);                                                                        break;  // BIT 6, H
            case 0x75:  INSTR_bit(6, m_registers.L);                                                                        break;  // BIT 6, L
            case 0x76:  INSTR_bit(6, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 6, (HL)
            case 0x77:  INSTR_bit(6, m_registers.A);                                                                        break;  // BIT 6, A
            case 0x78:  INSTR_bit(7, m_registers.B);                                                                        break;  // BIT 7, B
            case 0x79:  INSTR_bit(7, m_registers.C);                                                                        break;  // BIT 7, C
            case 0x7A:  INSTR_bit(7, m_registers.D);                                                                        break;  // BIT 7, D
            case 0x7B:  INSTR_bit(7, m_registers.E);                                                                        break;  // BIT 7, E
            case 0x7C:  INSTR_bit(7, m_registers.H);                                                                        break;  // BIT 7, H
            case 0x7D:  INSTR_bit(7, m_registers.L);                                                                        break;  // BIT 7, L
            case 0x7E:  INSTR_bit(7, MemReadByte(m_registers.HL)); DelayCycle();                                            break;  // BIT 7, (HL)
            case 0x7F:  INSTR_bit(7, m_registers.A);                                                                        break;  // BIT 7, A
            case 0x80:  m_registers.B = INSTR_res(0, m_registers.B);                                                        break;  // RES 0, B
            case 0x81:  m_registers.C = INSTR_res(0, m_registers.C);                                                        break;  // RES 0, C
            case 0x82:  m_registers.D = INSTR_res(0, m_registers.D);                                                        break;  // RES 0, D
            case 0x83:  m_registers.E = INSTR_res(0, m_registers.E);                                                        break;  // RES 0, E
            case 0x84:  m_registers.H = INSTR_res(0, m_registers.H);                                                        break;  // RES 0, H
            case 0x85:  m_registers.L = INSTR_res(0, m_registers.L);                                                        break;  // RES 0, L
            case 0x86:  MemWriteByte(m_registers.HL, INSTR_res(0, MemReadByte(m_registers.HL)));                            break;  // RES 0, (HL)
            case 0x87:  m_registers.A = INSTR_res(0, m_registers.A);                                                        break;  // RES 0, A
            case 0x88:  m_registers.B = INSTR_res(1, m_registers.B);                                                        break;  // RES 1, B
            case 0x89:  m_registers.C = INSTR_res(1, m_registers.C);                                                        break;  // RES 1, C
            case 0x8A:  m_registers.D = INSTR_res(1, m_registers.D);                                                        break;  // RES 1, D
            case 0x8B:  m_registers.E = INSTR_res(1, m_registers.E);                                                        break;  // RES 1, E
            case 0x8C:  m_registers.H = INSTR_res(1, m_registers.H);                                                        break;  // RES 1, H
            case 0x8D:  m_registers.L = INSTR_res(1, m_registers.L);                                                        break;  // RES 1, L
            case 0x8E:  MemWriteByte(m_registers.HL, INSTR_res(1, MemReadByte(m_registers.HL)));                            break;  // RES 1, (HL)
            case 0x8F:  m_registers.A = INSTR_res(1, m_registers.A);                                                        break;  // RES 1, A
            case 0x90:  m_registers.B = INSTR_res(2, m_registers.B);                                                        break;  // RES 2, B
            case 0x91:  m_registers.C = INSTR_res(2, m_registers.C);                                                        break;  // RES 2, C
            case 0x92:  m_registers.D = INSTR_res(2, m_registers.D);                                                        break;  // RES 2, D
            case 0x93:  m_registers.E = INSTR_res(2, m_registers.E);                                                        break;  // RES 2, E
            case 0x94:  m_registers.H = INSTR_res(2, m_registers.H);                                                        break;  // RES 2, H
            case 0x95:  m_registers.L = INSTR_res(2, m_registers.L);                                                        break;  // RES 2, L
            case 0x96:  MemWriteByte(m_registers.HL, INSTR_res(2, MemReadByte(m_registers.HL)));                            break;  // RES 2, (HL)
            case 0x97:  m_registers.A = INSTR_res(2, m_registers.A);                                                        break;  // RES 2, A
            case 0x98:  m_registers.B = INSTR_res(3, m_registers.B);                                                        break;  // RES 3, B
            case 0x99:  m_registers.C = INSTR_res(3, m_registers.C);                                                        break;  // RES 3, C
            case 0x9A:  m_registers.D = INSTR_res(3, m_registers.D);                                                        break;  // RES 3, D
            case 0x9B:  m_registers.E = INSTR_res(3, m_registers.E);                                                        break;  // RES 3, E
            case 0x9C:  m_registers.H = INSTR_res(3, m_registers.H);                                                        break;  // RES 3, H
            case 0x9D:  m_registers.L = INSTR_res(3, m_registers.L);                                                        break;  // RES 3, L
            case 0x9E:  MemWriteByte(m_registers.HL, INSTR_res(3, MemReadByte(m_registers.HL)));                            break;  // RES 3, (HL)
            case 0x9F:  m_registers.A = INSTR_res(3, m_registers.A);                                                        break;  // RES 3, A
            case 0xA0:  m_registers.B = INSTR_res(4, m_registers.B);                                                        break;  // RES 4, B
            case 0xA1:  m_registers.C = INSTR_res(4, m_registers.C);                                                        break;  // RES 4, C
            case 0xA2:  m_registers.D = INSTR_res(4, m_registers.D);                                                        break;  // RES 4, D
            case 0xA3:  m_registers.E = INSTR_res(4, m_registers.E);                                                        break;  // RES 4, E
            case 0xA4:  m_registers.H = INSTR_res(4, m_registers.H);                                                        break;  // RES 4, H
            case 0xA5:  m_registers.L = INSTR_res(4, m_registers.L);                                                        break;  // RES 4, L
            case 0xA6:  MemWriteByte(m_registers.HL, INSTR_res(4, MemReadByte(m_registers.HL)));                            break;  // RES 4, (HL)
            case 0xA7:  m_registers.A = INSTR_res(4, m_registers.A);                                                        break;  // RES 4, A
            case 0xA8:  m_registers.B = INSTR_res(5, m_registers.B);                                                        break;  // RES 5, B
            case 0xA9:  m_registers.C = INSTR_res(5, m_registers.C);                                                        break;  // RES 5, C
            case 0xAA:  m_registers.D = INSTR_res(5, m_registers.D);                                                        break;  // RES 5, D
            case 0xAB:  m_registers.E = INSTR_res(5, m_registers.E);                                                        break;  // RES 5, E
            case 0xAC:  m_registers.H = INSTR_res(5, m_registers.H);                                                        break;  // RES 5, H
            case 0xAD:  m_registers.L = INSTR_res(5, m_registers.L);                                                        break;  // RES 5, L
            case 0xAE:  MemWriteByte(m_registers.HL, INSTR_res(5, MemReadByte(m_registers.HL)));                            break;  // RES 5, (HL)
            case 0xAF:  m_registers.A = INSTR_res(5, m_registers.A);                                                        break;  // RES 5, A
            case 0xB0:  m_registers.B = INSTR_res(6, m_registers.B);                                                        break;  // RES 6, B
            case 0xB1:  m_registers.C = INSTR_res(6, m_registers.C);                                                        break;  // RES 6, C
            case 0xB2:  m_registers.D = INSTR_res(6, m_registers.D);                                                        break;  // RES 6, D
            case 0xB3:  m_registers.E = INSTR_res(6, m_registers.E);                                                        break;  // RES 6, E
            case 0xB4:  m_registers.H = INSTR_res(6, m_registers.H);                                                        break;  // RES 6, H
            case 0xB5:  m_registers.L = INSTR_res(6, m_registers.L);                                                        break;  // RES 6, L
            case 0xB6:  MemWriteByte(m_registers.HL, INSTR_res(6, MemReadByte(m_registers.HL)));                            break;  // RES 6, (HL)
            case 0xB7:  m_registers.A = INSTR_res(6, m_registers.A);                                                        break;  // RES 6, A
            case 0xB8:  m_registers.B = INSTR_res(7, m_registers.B);                                                        break;  // RES 7, B
            case 0xB9:  m_registers.C = INSTR_res(7, m_registers.C);                                                        break;  // RES 7, C
            case 0xBA:  m_registers.D = INSTR_res(7, m_registers.D);                                                        break;  // RES 7, D
            case 0xBB:  m_registers.E = INSTR_res(7, m_registers.E);                                                        break;  // RES 7, E
            case 0xBC:  m_registers.H = INSTR_res(7, m_registers.H);                                                        break;  // RES 7, H
            case 0xBD:  m_registers.L = INSTR_res(7, m_registers.L);                                                        break;  // RES 7, L
            case 0xBE:  MemWriteByte(m_registers.HL, INSTR_res(7, MemReadByte(m_registers.HL)));                            break;  // RES 7, (HL)
            case 0xBF:  m_registers.A = INSTR_res(7, m_registers.A);                                                        break;  // RES 7, A
            case 0xC0:  m_registers.B = INSTR_set(0, m_registers.B);                                                        break;  // SET 0, B
            case 0xC1:  m_registers.C = INSTR_set(0, m_registers.C);                                                        break;  // SET 0, C
            case 0xC2:  m_registers.D = INSTR_set(0, m_registers.D);                                                        break;  // SET 0, D
            case 0xC3:  m_registers.E = INSTR_set(0, m_registers.E);                                                        break;  // SET 0, E
            case 0xC4:  m_registers.H = INSTR_set(0, m_registers.H);                                                        break;  // SET 0, H
            case 0xC5:  m_registers.L = INSTR_set(0, m_registers.L);                                                        break;  // SET 0, L
            case 0xC6:  MemWriteByte(m_registers.HL, INSTR_set(0, MemReadByte(m_registers.HL)));                            break;  // SET 0, (HL)
            case 0xC7:  m_registers.A = INSTR_set(0, m_registers.A);                                                        break;  // SET 0, A
            case 0xC8:  m_registers.B = INSTR_set(1, m_registers.B);                                                        break;  // SET 1, B
            case 0xC9:  m_registers.C = INSTR_set(1, m_registers.C);                                                        break;  // SET 1, C
            case 0xCA:  m_registers.D = INSTR_set(1, m_registers.D);                                                        break;  // SET 1, D
            case 0xCB:  m_registers.E = INSTR_set(1, m_registers.E);                                                        break;  // SET 1, E
            case 0xCC:  m_registers.H = INSTR_set(1, m_registers.H);                                                        break;  // SET 1, H
            case 0xCD:  m_registers.L = INSTR_set(1, m_registers.L);                                                        break;  // SET 1, L
            case 0xCE:  MemWriteByte(m_registers.HL, INSTR_set(1, MemReadByte(m_registers.HL)));                            break;  // SET 1, (HL)
            case 0xCF:  m_registers.A = INSTR_set(1, m_registers.A);                                                        break;  // SET 1, A
            case 0xD0:  m_registers.B = INSTR_set(2, m_registers.B);                                                        break;  // SET 2, B
            case 0xD1:  m_registers.C = INSTR_set(2, m_registers.C);                                                        break;  // SET 2, C
            case 0xD2:  m_registers.D = INSTR_set(2, m_registers.D);                                                        break;  // SET 2, D
            case 0xD3:  m_registers.E = INSTR_set(2, m_registers.E);                                                        break;  // SET 2, E
            case 0xD4:  m_registers.H = INSTR_set(2, m_registers.H);                                                        break;  // SET 2, H
            case 0xD5:  m_registers.L = INSTR_set(2, m_registers.L);                                                        break;  // SET 2, L
            case 0xD6:  MemWriteByte(m_registers.HL, INSTR_set(2, MemReadByte(m_registers.HL)));                            break;  // SET 2, (HL)
            case 0xD7:  m_registers.A = INSTR_set(2, m_registers.A);                                                        break;  // SET 2, A
            case 0xD8:  m_registers.B = INSTR_set(3, m_registers.B);                                                        break;  // SET 3, B
            case 0xD9:  m_registers.C = INSTR_set(3, m_registers.C);                                                        break;  // SET 3, C
            case 0xDA:  m_registers.D = INSTR_set(3, m_registers.D);                                                        break;  // SET 3, D
            case 0xDB:  m_registers.E = INSTR_set(3, m_registers.E);                                                        break;  // SET 3, E
            case 0xDC:  m_registers.H = INSTR_set(3, m_registers.H);                                                        break;  // SET 3, H
            case 0xDD:  m_registers.L = INSTR_set(3, m_registers.L);                                                        break;  // SET 3, L
            case 0xDE:  MemWriteByte(m_registers.HL, INSTR_set(3, MemReadByte(m_registers.HL)));                            break;  // SET 3, (HL)
            case 0xDF:  m_registers.A = INSTR_set(3, m_registers.A);                                                        break;  // SET 3, A
            case 0xE0:  m_registers.B = INSTR_set(4, m_registers.B);                                                        break;  // SET 4, B
            case 0xE1:  m_registers.C = INSTR_set(4, m_registers.C);                                                        break;  // SET 4, C
            case 0xE2:  m_registers.D = INSTR_set(4, m_registers.D);                                                        break;  // SET 4, D
            case 0xE3:  m_registers.E = INSTR_set(4, m_registers.E);                                                        break;  // SET 4, E
            case 0xE4:  m_registers.H = INSTR_set(4, m_registers.H);                                                        break;  // SET 4, H
            case 0xE5:  m_registers.L = INSTR_set(4, m_registers.L);                                                        break;  // SET 4, L
            case 0xE6:  MemWriteByte(m_registers.HL, INSTR_set(4, MemReadByte(m_registers.HL)));                            break;  // SET 4, (HL)
            case 0xE7:  m_registers.A = INSTR_set(4, m_registers.A);                                                        break;  // SET 4, A
            case 0xE8:  m_registers.B = INSTR_set(5, m_registers.B);                                                        break;  // SET 5, B
            case 0xE9:  m_registers.C = INSTR_set(5, m_registers.C);                                                        break;  // SET 5, C
            case 0xEA:  m_registers.D = INSTR_set(5, m_registers.D);                                                        break;  // SET 5, D
            case 0xEB:  m_registers.E = INSTR_set(5, m_registers.E);                                                        break;  // SET 5, E
            case 0xEC:  m_registers.H = INSTR_set(5, m_registers.H);                                                        break;  // SET 5, H
            case 0xED:  m_registers.L = INSTR_set(5, m_registers.L);                                                        break;  // SET 5, L
            case 0xEE:  MemWriteByte(m_registers.HL, INSTR_set(5, MemReadByte(m_registers.HL)));                            break;  // SET 5, (HL)
            case 0xEF:  m_registers.A = INSTR_set(5, m_registers.A);                                                        break;  // SET 5, A
            case 0xF0:  m_registers.B = INSTR_set(6, m_registers.B);                                                        break;  // SET 6, B
            case 0xF1:  m_registers.C = INSTR_set(6, m_registers.C);                                                        break;  // SET 6, C
            case 0xF2:  m_registers.D = INSTR_set(6, m_registers.D);                                                        break;  // SET 6, D
            case 0xF3:  m_registers.E = INSTR_set(6, m_registers.E);                                                        break;  // SET 6, E
            case 0xF4:  m_registers.H = INSTR_set(6, m_registers.H);                                                        break;  // SET 6, H
            case 0xF5:  m_registers.L = INSTR_set(6, m_registers.L);                                                        break;  // SET 6, L
            case 0xF6:  MemWriteByte(m_registers.HL, INSTR_set(6, MemReadByte(m_registers.HL)));                            break;  // SET 6, (HL)
            case 0xF7:  m_registers.A = INSTR_set(6, m_registers.A);                                                        break;  // SET 6, A
            case 0xF8:  m_registers.B = INSTR_set(7, m_registers.B);                                                        break;  // SET 7, B
            case 0xF9:  m_registers.C = INSTR_set(7, m_registers.C);                                                        break;  // SET 7, C
            case 0xFA:  m_registers.D = INSTR_set(7, m_registers.D);                                                        break;  // SET 7, D
            case 0xFB:  m_registers.E = INSTR_set(7, m_registers.E);                                                        break;  // SET 7, E
            case 0xFC:  m_registers.H = INSTR_set(7, m_registers.H);                                                        break;  // SET 7, H
            case 0xFD:  m_registers.L = INSTR_set(7, m_registers.L);                                                        break;  // SET 7, L
            case 0xFE:  MemWriteByte(m_registers.HL, INSTR_set(7, MemReadByte(m_registers.HL)));                            break;  // SET 7, (HL)
            case 0xFF:  m_registers.A = INSTR_set(7, m_registers.A);                                                        break;  // SET 7, A
            }
        }
        break;

        // Unknown opcode
    default:
        UnreachableCode();
        break;
    }
}
