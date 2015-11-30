#include "jit_table.h"

// http://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html
// http://gameboy.mongenel.com/dmg/opcodes.html
// http://imrannazar.com/GameBoy-Emulation-in-JavaScript:-The-CPU
// http://stackoverflow.com/questions/8034566/overflow-and-carry-flags-on-z80

#define Operand(mode, reg) { mode, reg }
#define NoOperand() { Instruction::NumAddressModes }
#define Imm8() Operand(Instruction::AddressMode_Imm8, CPU::NumReg8)
#define Imm16() Operand(Instruction::AddressMode_Imm16, CPU::NumReg8)
#define Reg8(reg) Operand(Instruction::AddressMode_Reg8, (CPU::Reg8)reg)
#define Reg16(reg) Operand(Instruction::AddressMode_Reg16, (CPU::Reg8)reg)
#define Mem8(reg) Operand(Instruction::AddressMode_Mem8, (CPU::Reg8)reg)
#define Mem16(reg) Operand(Instruction::AddressMode_Mem16, (CPU::Reg8)reg)
#define Addr8() Operand(Instruction::AddressMode_Addr8, CPU::NumReg8)
#define Addr16() Operand(Instruction::AddressMode_Addr16, CPU::NumReg8)
#define AF CPU::Reg16_AF
#define BC CPU::Reg16_BC
#define DE CPU::Reg16_DE
#define HL CPU::Reg16_HL
#define SP CPU::Reg16_SP
#define PC CPU::Reg16_PC
#define A CPU::Reg8_A
#define F CPU::Reg8_F
#define B CPU::Reg8_B
#define C CPU::Reg8_C
#define D CPU::Reg8_D
#define E CPU::Reg8_E
#define H CPU::Reg8_H
#define L CPU::Reg8_L
#define Always Instruction::Predicate_Always
#define Zero Instruction::Predicate_Zero
#define Carry Instruction::Predicate_Carry
#define NotZero Instruction::Predicate_NotZero
#define NotCarry Instruction::Predicate_NotCarry
#define IncrementAddress Instruction::LoadStoreAction_IncrementAddress
#define DecrementAddress Instruction::LoadStoreAction_DecrementAddress

#define Stub(length, cycles) { Instruction::Type_Stub, NoOperand(), NoOperand(), length, cycles },
#define Nop(length, cycles) { Instruction::Type_Nop, NoOperand(), NoOperand(), length, cycles },
#define Load(dst, src, length, cycles) { Instruction::Type_Load, dst, src, length, cycles },
#define Store(dst, src, length, cycles) { Instruction::Type_Store, dst, src, length, cycles },
#define Move(dst, src, length, cycles)  { Instruction::Type_Move, dst, src, length, cycles },
#define LoadAnd(action, dst, src, length, cycles) { Instruction::Type_Load, dst, src, length, cycles, action },
#define StoreAnd(action, dst, src, length, cycles) { Instruction::Type_Store, dst, src, length, cycles, action },
#define READIO(offset, length, cycles) { Instruction::Type_READIO, offset, NoOperand(), length, cycles },
#define WRITEIO(offset, length, cycles) { Instruction::Type_WRITEIO, offset, NoOperand(), length, cycles },
#define INC(dst, length, cycles) { Instruction::Type_INC, dst, NoOperand(), length, cycles },
#define INC16(dst, length, cycles) { Instruction::Type_INC16, dst, NoOperand(), length, cycles },
#define DEC(dst, length, cycles) { Instruction::Type_DEC, dst, NoOperand(), length, cycles },
#define DEC16(dst, length, cycles) { Instruction::Type_DEC16, dst, NoOperand(), length, cycles },
#define ADD(src, length, cycles) { Instruction::Type_ADD, src, NoOperand(), length, cycles },
#define ADD16(dst, src, length, cycles) { Instruction::Type_ADD16, dst, src, length, cycles },
#define ADDS8(dst, src, length, cycles) { Instruction::Type_ADDS8, dst, src, length, cycles },
#define ADC(src, length, cycles) { Instruction::Type_ADC, src, NoOperand(), length, cycles },
#define SUB(src, length, cycles) { Instruction::Type_SUB, src, NoOperand(), length, cycles },
#define SBC(src, length, cycles) { Instruction::Type_SBC, src, NoOperand(), length, cycles },
#define RL(dst, length, cycles) { Instruction::Type_RL, dst, NoOperand(), length, cycles }, 
#define RR(dst, length, cycles) { Instruction::Type_RR, dst, NoOperand(), length, cycles }, 
#define RLC(dst, length, cycles) { Instruction::Type_RLC, dst, NoOperand(), length, cycles }, 
#define RRC(dst, length, cycles) { Instruction::Type_RRC, dst, NoOperand(), length, cycles }, 
#define AND(src, length, cycles) { Instruction::Type_AND, src, NoOperand(), length, cycles },
#define OR(src, length, cycles) { Instruction::Type_OR, src, NoOperand(), length, cycles },
#define XOR(src, length, cycles) { Instruction::Type_XOR, src, NoOperand(), length, cycles },
#define CP(src, length, cycles) { Instruction::Type_CP, src, NoOperand(), length, cycles },
#define CPL(length, cycles) { Instruction::Type_CPL, NoOperand(), NoOperand(), length, cycles },
#define SWAP(dst, length, cycles) { Instruction::Type_SWAP, dst, NoOperand(), length, cycles },
#define BIT(bitnum, src, length, cycles) { Instruction::Type_BIT, NoOperand(), src, length, cycles, (Instruction::LoadStoreAction)bitnum },
#define SET(bitnum, dst, length, cycles) { Instruction::Type_SET, dst, NoOperand(), length, cycles, (Instruction::LoadStoreAction)bitnum },
#define RES(bitnum, dst, length, cycles) { Instruction::Type_RES, dst, NoOperand(), length, cycles, (Instruction::LoadStoreAction)bitnum },
#define JR(predicate, length, cycles, cycles_skipped) { Instruction::Type_JR, Imm8(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define JP(predicate, src, length, cycles, cycles_skipped) { Instruction::Type_JP, src, NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define CALL(predicate, length, cycles, cycles_skipped) { Instruction::Type_CALL, Imm16(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define RET(predicate, length, cycles, cycles_skipped) { Instruction::Type_RET, NoOperand(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define RETI(length, cycles) { Instruction::Type_RETI, NoOperand(), NoOperand(), length, cycles },
#define PUSH(src, length, cycles) { Instruction::Type_PUSH, NoOperand(), src, length, cycles },
#define POP(dst, length, cycles) { Instruction::Type_POP, dst, NoOperand(), length, cycles },
#define RST(vector, length, cycles) { Instruction::Type_RST, { Instruction::NumAddressModes, (CPU::Reg8)vector }, NoOperand(), length, cycles },
#define CCF(length, cycles) { Instruction::Type_CCF, NoOperand(), NoOperand(), length, cycles },
#define SCF(length, cycles) { Instruction::Type_SCF, NoOperand(), NoOperand(), length, cycles },
#define HALT(length, cycles) { Instruction::Type_HALT, NoOperand(), NoOperand(), length, cycles },
#define STOP(length, cycles) { Instruction::Type_STOP, NoOperand(), NoOperand(), length, cycles },
#define EI(length, cycles) { Instruction::Type_EI, NoOperand(), NoOperand(), length, cycles },
#define DI(length, cycles) { Instruction::Type_DI, NoOperand(), NoOperand(), length, cycles },
#define LDHL_SPR8(length, cycles) { Instruction::Type_LDHL_SPR8, NoOperand(), NoOperand(), length, cycles },
#define SLA(dst, length, cycles) { Instruction::Type_SLA, dst, NoOperand(), length, cycles },
#define SRA(dst, length, cycles) { Instruction::Type_SRA, dst, NoOperand(), length, cycles },
#define SRL(dst, length, cycles) { Instruction::Type_SRL, dst, NoOperand(), length, cycles },
#define DAA(length, cycles) { Instruction::Type_DAA, NoOperand(), NoOperand(), length, cycles },
#define PREFIX() { Instruction::Type_Prefix, NoOperand(), NoOperand(), 1, 4 },

namespace JitTable {

const Instruction instructions[256] =
{
    // 0x00 - 0x0F
    Nop(1, 4)                                               // 0x00 NOP
    Load(Reg16(BC), Imm16(), 3, 12)                         // 0x01 LD BC, d16
    Store(Mem16(BC), Reg8(A), 1, 8)                         // 0x02 LD (BC), A
    INC16(Reg16(BC), 1, 8)                              // 0x03 INC BC
    INC(Reg8(B), 1, 4)                                // 0x04 INC B
    DEC(Reg8(B), 1, 4)                                // 0x05 DEC B
    Load(Reg8(B), Imm8(), 2, 8)                             // 0x06 LD B, d8
    RLC(Reg8(A), 1, 4)                  // 0x07 RLCA
    Store(Addr16(), Reg16(SP), 3, 20)                       // 0x08 LD (a16), SP
    ADD16(Reg16(HL), Reg16(BC), 1, 8)           // 0x09 ADD HL, BC
    Load(Reg8(A), Mem16(BC), 1, 8)                          // 0x0A LD A, (BC)
    DEC16(Reg16(BC), 1, 8)                              // 0x0B DEC BC
    INC(Reg8(C), 1, 4)                                // 0x0C INC C
    DEC(Reg8(C), 1, 4)                                // 0x0D DEC C
    Load(Reg8(C), Imm8(), 2, 8)                             // 0x0E LD C, d8
    RRC(Reg8(A), 1, 4)                 // 0x0F RRCA

    // 0x10 - 0x1F
    STOP(2, 4)                                              // 0x10 STOP 0
    Load(Reg16(DE), Imm16(), 3, 12)                         // 0x11 LD DE, d16
    Store(Mem16(DE), Reg8(A), 1, 8)                         // 0x12 LD (DE), A
    INC16(Reg16(DE), 1, 8)                              // 0x13 INC DE
    INC(Reg8(D), 1, 4)                                // 0x14 INC D
    DEC(Reg8(D), 1, 4)                                // 0x15 DEC D
    Load(Reg8(D), Imm8(), 2, 8)                             // 0x16 LD D, d8
    RL(Reg8(A), 1, 4)               // 0x17 RLA
    JR(Always, 2, 12, 12)                         // 0x18 JR r8
    ADD16(Reg16(HL), Reg16(DE), 1, 8)           // 0x19 ADD HL, DE
    Load(Reg8(A), Mem16(DE), 1, 8)                          // 0x1A LD A, (DE)
    DEC16(Reg16(DE), 1, 8)                              // 0x1B DEC DE
    INC(Reg8(E), 1, 4)                                // 0x1C INC E
    DEC(Reg8(E), 1, 4)                                // 0x1D DEC E
    Load(Reg8(E), Imm8(), 2, 8)                             // 0x1E LD E, d8
    RR(Reg8(A), 1, 4)              // 0x1F RRA

    // 0x20 - 0x2F
    JR(NotZero, 2, 12, 8)                         // 0x20 JR NZ, r8
    Load(Reg16(HL), Imm16(), 3, 12)                         // 0x21 LD HL, d16
    StoreAnd(IncrementAddress, Mem16(HL), Reg8(A), 1, 8)    // 0x22 LDI (HL), A
    INC16(Reg16(HL), 1, 8)                              // 0x23 INC HL
    INC(Reg8(H), 1, 4)                                // 0x24 INC H
    DEC(Reg8(H), 1, 4)                                // 0x25 DEC H
    Load(Reg8(H), Imm8(), 2, 8)                             // 0x26 LD H, d8
    DAA(1, 4)                                              // 0x27 DAA
    JR(Zero, 2, 12, 8)                            // 0x28 JR Z, r8
    ADD16(Reg16(HL), Reg16(HL), 1, 8)           // 0x29 ADD HL, HL
    LoadAnd(IncrementAddress, Reg8(A), Mem16(HL), 1, 8)     // 0x2A LDI A, (HL)
    DEC16(Reg16(HL), 1, 8)                              // 0x2B DEC HL
    INC(Reg8(L), 1, 4)                                // 0x2C INC L
    DEC(Reg8(L), 1, 4)                                // 0x2D DEC L
    Load(Reg8(L), Imm8(), 2, 8)                             // 0x2E LD L, d8
    CPL(1, 4)                             // 0x2F CPL

    // 0x30 - 0x3F
    JR(NotCarry, 2, 12, 8)                        // 0x30 JR NC, r8
    Load(Reg16(SP), Imm16(), 3, 12)                         // 0x31 LD SP, d16
    StoreAnd(DecrementAddress, Mem16(HL), Reg8(A), 1, 8)    // 0x32 LDD (HL), A
    INC16(Reg16(SP), 1, 8)                              // 0x33 INC SP
    INC(Mem16(HL), 1, 12)                             // 0x34 INC (HL)
    DEC(Mem16(HL), 1, 12)                             // 0x35 DEC (HL)
    Store(Mem16(HL), Imm8(), 2, 12)                         // 0x36 LD (HL), d8
    SCF(1, 4)                                               // 0x37 SCF
    JR(Carry, 2, 12, 8)                           // 0x38 JR C, r8
    ADD16(Reg16(HL), Reg16(SP), 1, 8)           // 0x39 ADD HL, SP
    LoadAnd(DecrementAddress, Reg8(A), Mem16(HL), 1, 8)     // 0x3A LDD A, (HL)
    DEC16(Reg16(SP), 1, 8)                              // 0x3B DEC SP
    INC(Reg8(A), 1, 4)                                // 0x3C INC A
    DEC(Reg8(A), 1, 4)                                // 0x3D DEC A
    Load(Reg8(A), Imm8(), 2, 8)                             // 0x3E LD A, d8
    CCF(1, 4)                                               // 0x3F CCF
    Move(Reg8(B), Reg8(B), 1, 4)                            // 0x40 LD B, B
    Move(Reg8(B), Reg8(C), 1, 4)                            // 0x41 LD B, C
    Move(Reg8(B), Reg8(D), 1, 4)                            // 0x42 LD B, D
    Move(Reg8(B), Reg8(E), 1, 4)                            // 0x43 LD B, E
    Move(Reg8(B), Reg8(H), 1, 4)                            // 0x44 LD B, H
    Move(Reg8(B), Reg8(L), 1, 4)                            // 0x45 LD B, L
    Load(Reg8(B), Mem16(HL), 1, 8)                          // 0x46 LD B, (HL)
    Move(Reg8(B), Reg8(A), 1, 4)                            // 0x47 LD B, A
    Move(Reg8(C), Reg8(B), 1, 4)                            // 0x48 LD C, B
    Move(Reg8(C), Reg8(C), 1, 4)                            // 0x49 LD C, C
    Move(Reg8(C), Reg8(D), 1, 4)                            // 0x4A LD C, D
    Move(Reg8(C), Reg8(E), 1, 4)                            // 0x4B LD C, E
    Move(Reg8(C), Reg8(H), 1, 4)                            // 0x4C LD C, H
    Move(Reg8(C), Reg8(L), 1, 4)                            // 0x4D LD C, L
    Load(Reg8(C), Mem16(HL), 1, 8)                          // 0x4E LD C, (HL)
    Move(Reg8(C), Reg8(A), 1, 4)                            // 0x4F LD C, A
    Move(Reg8(D), Reg8(B), 1, 4)                            // 0x50 LD D, B
    Move(Reg8(D), Reg8(C), 1, 4)                            // 0x51 LD D, C
    Move(Reg8(D), Reg8(D), 1, 4)                            // 0x52 LD D, D
    Move(Reg8(D), Reg8(E), 1, 4)                            // 0x53 LD D, E
    Move(Reg8(D), Reg8(H), 1, 4)                            // 0x54 LD D, H
    Move(Reg8(D), Reg8(L), 1, 4)                            // 0x55 LD D, L
    Load(Reg8(D), Mem16(HL), 1, 8)                          // 0x56 LD D, (HL)
    Move(Reg8(D), Reg8(A), 1, 4)                            // 0x57 LD D, A
    Move(Reg8(E), Reg8(B), 1, 4)                            // 0x58 LD E, B
    Move(Reg8(E), Reg8(C), 1, 4)                            // 0x59 LD E, C
    Move(Reg8(E), Reg8(D), 1, 4)                            // 0x5A LD E, D
    Move(Reg8(E), Reg8(E), 1, 4)                            // 0x5B LD E, E
    Move(Reg8(E), Reg8(H), 1, 4)                            // 0x5C LD E, H
    Move(Reg8(E), Reg8(L), 1, 4)                            // 0x5D LD E, L
    Load(Reg8(E), Mem16(HL), 1, 8)                          // 0x5E LD E, (HL)
    Move(Reg8(E), Reg8(A), 1, 4)                            // 0x5F LD E, A
    Move(Reg8(H), Reg8(B), 1, 4)                            // 0x60 LD H, B
    Move(Reg8(H), Reg8(C), 1, 4)                            // 0x61 LD H, C
    Move(Reg8(H), Reg8(D), 1, 4)                            // 0x62 LD H, D
    Move(Reg8(H), Reg8(E), 1, 4)                            // 0x63 LD H, E
    Move(Reg8(H), Reg8(H), 1, 4)                            // 0x64 LD H, H
    Move(Reg8(H), Reg8(L), 1, 4)                            // 0x65 LD H, L
    Load(Reg8(H), Mem16(HL), 1, 8)                          // 0x66 LD H, (HL)
    Move(Reg8(H), Reg8(A), 1, 4)                            // 0x67 LD H, A
    Move(Reg8(L), Reg8(B), 1, 4)                            // 0x68 LD L, B
    Move(Reg8(L), Reg8(C), 1, 4)                            // 0x69 LD L, C
    Move(Reg8(L), Reg8(D), 1, 4)                            // 0x6A LD L, D
    Move(Reg8(L), Reg8(E), 1, 4)                            // 0x6B LD L, E
    Move(Reg8(L), Reg8(H), 1, 4)                            // 0x6C LD L, H
    Move(Reg8(L), Reg8(L), 1, 4)                            // 0x6D LD L, L
    Load(Reg8(L), Mem16(HL), 1, 8)                          // 0x6E LD L, (HL)
    Move(Reg8(L), Reg8(A), 1, 4)                            // 0x6F LD L, A
    Store(Mem16(HL), Reg8(B), 1, 8)                         // 0x70 LD (HL), B
    Store(Mem16(HL), Reg8(C), 1, 8)                         // 0x71 LD (HL), C
    Store(Mem16(HL), Reg8(D), 1, 8)                         // 0x72 LD (HL), D
    Store(Mem16(HL), Reg8(E), 1, 8)                         // 0x73 LD (HL), E
    Store(Mem16(HL), Reg8(H), 1, 8)                         // 0x74 LD (HL), H
    Store(Mem16(HL), Reg8(L), 1, 8)                         // 0x75 LD (HL), L
    HALT(1, 4)                                              // 0x76 HALT
    Store(Mem16(HL), Reg8(A), 1, 8)                         // 0x77 LD (HL), A
    Move(Reg8(A), Reg8(B), 1, 4)                            // 0x78 LD A, B
    Move(Reg8(A), Reg8(C), 1, 4)                            // 0x79 LD A, C
    Move(Reg8(A), Reg8(D), 1, 4)                            // 0x7A LD A, D
    Move(Reg8(A), Reg8(E), 1, 4)                            // 0x7B LD A, E
    Move(Reg8(A), Reg8(H), 1, 4)                            // 0x7C LD A, H
    Move(Reg8(A), Reg8(L), 1, 4)                            // 0x7D LD A, L
    Load(Reg8(A), Mem16(HL), 1, 8)                          // 0x7E LD A, (HL)
    Move(Reg8(A), Reg8(A), 1, 4)                            // 0x7F LD A, A
    ADD(Reg8(B), 1, 4)               // 0x80 ADD A, B
    ADD(Reg8(C), 1, 4)               // 0x81 ADD A, C
    ADD(Reg8(D), 1, 4)               // 0x82 ADD A, D
    ADD(Reg8(E), 1, 4)               // 0x83 ADD A, E
    ADD(Reg8(H), 1, 4)               // 0x84 ADD A, H
    ADD(Reg8(L), 1, 4)               // 0x85 ADD A, L
    ADD(Mem16(HL), 1, 8)             // 0x86 ADD A, (HL)
    ADD(Reg8(A), 1, 4)               // 0x87 ADD A, A
    ADC(Reg8(B), 1, 4)                  // 0x88 ADC A, B
    ADC(Reg8(C), 1, 4)                  // 0x89 ADC A, C
    ADC(Reg8(D), 1, 4)                  // 0x8A ADC A, D
    ADC(Reg8(E), 1, 4)                  // 0x8B ADC A, E
    ADC(Reg8(H), 1, 4)                  // 0x8C ADC A, H
    ADC(Reg8(L), 1, 4)                  // 0x8D ADC A, L
    ADC(Mem16(HL), 1, 8)                // 0x8E ADC A, (HL)
    ADC(Reg8(A), 1, 4)                  // 0x8F ADC A, A
    SUB(Reg8(B), 1, 4)               // 0x90 SUB B
    SUB(Reg8(C), 1, 4)               // 0x91 SUB C
    SUB(Reg8(D), 1, 4)               // 0x92 SUB D
    SUB(Reg8(E), 1, 4)               // 0x93 SUB E
    SUB(Reg8(H), 1, 4)               // 0x94 SUB H
    SUB(Reg8(L), 1, 4)               // 0x95 SUB L
    SUB(Mem16(HL), 1, 8)             // 0x96 SUB (HL)
    SUB(Reg8(A), 1, 4)               // 0x97 SUB A
    SBC(Reg8(B), 1, 4)                  // 0x98 SBC A, B
    SBC(Reg8(C), 1, 4)                  // 0x99 SBC A, C
    SBC(Reg8(D), 1, 4)                  // 0x9A SBC A, D
    SBC(Reg8(E), 1, 4)                  // 0x9B SBC A, E
    SBC(Reg8(H), 1, 4)                  // 0x9C SBC A, H
    SBC(Reg8(L), 1, 4)                  // 0x9D SBC A, L
    SBC(Mem16(HL), 1, 8)                // 0x9E SBC A, (HL)
    SBC(Reg8(A), 1, 4)                  // 0x9F SBC A, A
    AND(Reg8(B), 1, 4)                             // 0xA0 AND B
    AND(Reg8(C), 1, 4)                             // 0xA1 AND C
    AND(Reg8(D), 1, 4)                             // 0xA2 AND D
    AND(Reg8(E), 1, 4)                             // 0xA3 AND E
    AND(Reg8(H), 1, 4)                             // 0xA4 AND H
    AND(Reg8(L), 1, 4)                             // 0xA5 AND L
    AND(Mem16(HL), 1, 8)                           // 0xA6 AND (HL)
    AND(Reg8(A), 1, 4)                             // 0xA7 AND A
    XOR(Reg8(B), 1, 4)                             // 0xA8 XOR B
    XOR(Reg8(C), 1, 4)                             // 0xA9 XOR C
    XOR(Reg8(D), 1, 4)                             // 0xAA XOR D
    XOR(Reg8(E), 1, 4)                             // 0xAB XOR E
    XOR(Reg8(H), 1, 4)                             // 0xAC XOR H
    XOR(Reg8(L), 1, 4)                             // 0xAD XOR L
    XOR(Mem16(HL), 1, 8)                           // 0xAE XOR (HL)
    XOR(Reg8(A), 1, 4)                             // 0xAF XOR A
    OR(Reg8(B), 1, 4)                              // 0xB0 OR B
    OR(Reg8(C), 1, 4)                              // 0xB1 OR C
    OR(Reg8(D), 1, 4)                              // 0xB2 OR D
    OR(Reg8(E), 1, 4)                              // 0xB3 OR E
    OR(Reg8(H), 1, 4)                              // 0xB4 OR H
    OR(Reg8(L), 1, 4)                              // 0xB5 OR L
    OR(Mem16(HL), 1, 8)                            // 0xB6 OR (HL)
    OR(Reg8(A), 1, 4)                              // 0xB7 OR A
    CP(Reg8(B), 1, 4)                             // 0xB8 CP B
    CP(Reg8(C), 1, 4)                             // 0xB9 CP C
    CP(Reg8(D), 1, 4)                             // 0xBA CP D
    CP(Reg8(E), 1, 4)                             // 0xBB CP E
    CP(Reg8(H), 1, 4)                             // 0xBC CP H
    CP(Reg8(L), 1, 4)                             // 0xBD CP L
    CP(Mem16(HL), 1, 8)                           // 0xBE CP (HL)
    CP(Reg8(A), 1, 4)                             // 0xBF CP A
    RET(NotZero, 1, 20, 8)                               // 0xC0 RET NZ
    POP(Reg16(BC), 1, 12)                                   // 0xC1 POP BC
    JP(NotZero, Imm16(), 3, 16, 12)               // 0xC2 JP NZ, a16
    JP(Always, Imm16(), 3, 16, 16)                // 0xC3 JP a16
    CALL(NotZero, 3, 24, 12)                                // 0xC4 CALL NZ, a16
    PUSH(Reg16(BC), 1, 16)                                  // 0xC5 PUSH BC
    ADD(Imm8(), 2, 8)                // 0xC6 ADD A, d8
    RST(0, 1, 16)                                           // 0xC7 RST 00H
    RET(Zero, 1, 20, 8)                                  // 0xC8 RET Z
    RET(Always, 1, 16, 16)                               // 0xC9 RET
    JP(Zero, Imm16(), 3, 16, 12)                  // 0xCA JP Z, a16
    PREFIX()                                                // 0xCB PREFIX CB
    CALL(Zero, 3, 24, 12)                                   // 0xCC CALL Z, a16
    CALL(Always, 3, 24, 12)                                 // 0xCD CALL a16
    ADC(Imm8(), 2, 8)                   // 0xCE ADC A, d8
    RST(0x08, 1, 16)                                        // 0xCF RST 08H
    RET(NotCarry, 1, 20, 8)                              // 0xD0 RET NC
    POP(Reg16(DE), 1, 12)                                   // 0xD1 POP DE
    JP(NotCarry, Imm16(), 3, 16, 12)              // 0xD2 JP NC, a16
    Stub(1, 0)                                              // 0xD3
    CALL(NotCarry, 3, 24, 12)                               // 0xD4 CALL NC, a16
    PUSH(Reg16(DE), 1, 16)                                  // 0xD5 PUSH DE
    SUB(Imm8(), 2, 8)                // 0xD6 SUB d8
    RST(0x10, 1, 16)                                        // 0xD7 RST 10H
    RET(Carry, 1, 20, 8)                                    // 0xD8 RET C
    RETI(1, 16)                                             // 0xD9 RETI
    JP(Carry, Imm16(), 3, 16, 12)                 // 0xDA JP C, a16
    Stub(1, 0)                                              // 0xDB
    CALL(Carry, 3, 24, 12)                                  // 0xDC CALL C, a16
    Stub(1, 0)                                              // 0xDD
    SBC(Imm8(), 2, 8)                   // 0xDE SBC A, d8
    RST(0x18, 1, 16)                                        // 0xDF RST 18H
    WRITEIO(Imm8(), 2, 12)                      // 0xE0 LDH (a8), A
    POP(Reg16(HL), 1, 12)                                   // 0xE1 POP HL
    WRITEIO(Reg8(C), 1, 8)                      // 0xE2 LD (C), A
    Stub(1, 0)                                              // 0xE3
    Stub(1, 0)                                              // 0xE4
    PUSH(Reg16(HL), 1, 16)                                  // 0xE5 PUSH HL
    AND(Imm8(), 2, 8)                              // 0xE6 AND d8
    RST(0x20, 1, 16)                                        // 0xE7 RST 20H
    ADDS8(Reg16(SP), Imm8(), 2, 16)             // 0xE8 ADD SP, r8
    JP(Always, Reg16(HL), 1, 4, 4)                // 0xE9 JP (HL)
    Store(Addr16(), Reg8(A), 3, 16)                         // 0xEA LD (a16), A
    Stub(1, 0)                                              // 0xEB
    Stub(1, 0)                                              // 0xEC
    Stub(1, 0)                                              // 0xED
    XOR(Imm8(), 2, 8)                              // 0xEE XOR d8
    RST(0x28, 1, 16)                                        // 0xEF RST 28H
    READIO(Imm8(), 2, 12)                       // 0xF0 LDH A, (a8)
    POP(Reg16(AF), 1, 12)                                   // 0xF1 POP AF
    READIO(Reg8(C), 1, 8)                       // 0xF2 LD A, (C)
    DI(1, 4)                                                // 0xF3 DI
    Stub(1, 0)                                              // 0xF4
    PUSH(Reg16(AF), 1, 16)                                  // 0xF5 PUSH AF
    OR(Imm8(), 2, 8)                               // 0xF6 OR d8
    RST(0x30, 1, 16)                                        // 0xF7 RST 30H
    LDHL_SPR8(2, 12)                                        // 0xF8 LD HL, SP+r8
    Move(Reg16(SP), Reg16(HL), 1, 8)                        // 0xF9 LD SP, HL
    Load(Reg8(A), Addr16(), 3, 16)                          // 0xFA LD A, (a16)
    EI(1, 4)                                                // 0xFB EI
    Stub(1, 0)                                              // 0xFC
    Stub(1, 0)                                              // 0xFD
    CP(Imm8(), 2, 8)                              // 0xFE CP d8
    RST(0x38, 1, 16)                                        // 0xFF RST 38H
};

const Instruction cb_instructions[256] =
{
    RLC(Reg8(B), 2, 8)                  // 0x00 RLC B
    RLC(Reg8(C), 2, 8)                  // 0x01 RLC C
    RLC(Reg8(D), 2, 8)                  // 0x02 RLC D
    RLC(Reg8(E), 2, 8)                  // 0x03 RLC E
    RLC(Reg8(H), 2, 8)                  // 0x04 RLC H
    RLC(Reg8(L), 2, 8)                  // 0x05 RLC L
    RLC(Mem16(B), 2, 16)                // 0x06 RLC (HL)
    RLC(Reg8(A), 2, 8)                  // 0x07 RLC A
    RRC(Reg8(B), 2, 8)                 // 0x08 RRC B
    RRC(Reg8(C), 2, 8)                 // 0x09 RRC C
    RRC(Reg8(D), 2, 8)                 // 0x0A RRC D
    RRC(Reg8(E), 2, 8)                 // 0x0B RRC E
    RRC(Reg8(H), 2, 8)                 // 0x0C RRC H
    RRC(Reg8(L), 2, 8)                 // 0x0D RRC L
    RRC(Mem16(HL), 2, 16)              // 0x0E RRC (HL)
    RRC(Reg8(A), 2, 8)                 // 0x0F RRC A
    RL(Reg8(B), 2, 8)               // 0x10 RL B
    RL(Reg8(C), 2, 8)               // 0x11 RL C
    RL(Reg8(D), 2, 8)               // 0x12 RL D
    RL(Reg8(E), 2, 8)               // 0x13 RL E
    RL(Reg8(H), 2, 8)               // 0x14 RL H
    RL(Reg8(L), 2, 8)               // 0x15 RL L
    RL(Mem16(HL), 2, 16)            // 0x16 RL (HL)
    RL(Reg8(A), 2, 8)              // 0x17 RL A
    RR(Reg8(B), 2, 8)              // 0x18 RR B
    RR(Reg8(C), 2, 8)              // 0x19 RR C
    RR(Reg8(D), 2, 8)              // 0x1A RR D
    RR(Reg8(E), 2, 8)              // 0x1B RR E
    RR(Reg8(H), 2, 8)              // 0x1C RR H
    RR(Reg8(L), 2, 8)              // 0x1D RR L
    RR(Mem16(HL), 2, 16)           // 0x1E RR (HL)
    RR(Reg8(A), 2, 8)              // 0x1F RR A
    SLA(Reg8(B), 2, 8)                                      // 0x20 SLA B
    SLA(Reg8(C), 2, 8)                  // 0x21 SLA C
    SLA(Reg8(D), 2, 8)                                      // 0x22 SLA D
    SLA(Reg8(E), 2, 8)                                      // 0x23 SLA E
    SLA(Reg8(H), 2, 8)                                      // 0x24 SLA H
    SLA(Reg8(L), 2, 8)                                      // 0x25 SLA L
    SLA(Mem16(HL), 2, 16)                                   // 0x26 SLA (HL)
    SLA(Reg8(A), 2, 8)                                      // 0x27 SLA A
    SRA(Reg8(B), 2, 8)                                      // 0x28 SRA B
    SRA(Reg8(C), 2, 8)                                      // 0x29 SRA C
    SRA(Reg8(D), 2, 8)                                      // 0x2A SRA D
    SRA(Reg8(E), 2, 8)                                      // 0x2B SRA E
    SRA(Reg8(H), 2, 8)                                      // 0x2C SRA H
    SRA(Reg8(L), 2, 8)                                      // 0x2D SRA L
    SRA(Mem16(HL), 2, 16)                                   // 0x2E SRA (HL)
    SRA(Reg8(A), 2, 8)                                      // 0x2F SRA A
    SWAP(Reg8(B), 2, 8)                            // 0x30 SWAP B
    SWAP(Reg8(C), 2, 8)                            // 0x31 SWAP C
    SWAP(Reg8(D), 2, 8)                            // 0x32 SWAP D
    SWAP(Reg8(E), 2, 8)                            // 0x33 SWAP E
    SWAP(Reg8(H), 2, 8)                            // 0x34 SWAP H
    SWAP(Reg8(L), 2, 8)                            // 0x35 SWAP L
    SWAP(Mem16(HL), 2, 16)                       // 0x36 SWAP (HL)
    SWAP(Reg8(A), 2, 8)                            // 0x37 SWAP A
    SRL(Reg8(B), 2, 8)                                      // 0x38 SRL B
    SRL(Reg8(C), 2, 8)                                      // 0x39 SRL C
    SRL(Reg8(D), 2, 8)                                      // 0x3A SRL D
    SRL(Reg8(E), 2, 8)                                      // 0x3B SRL E
    SRL(Reg8(H), 2, 8)                                      // 0x3C SRL H
    SRL(Reg8(L), 2, 8)                                      // 0x3D SRL L
    SRL(Mem16(HL), 2, 16)                                   // 0x3E SRL (HL)
    SRL(Reg8(A), 2, 8)                                      // 0x3F SRL A
    BIT(0, Reg8(B), 2, 8)                               // 0x40 BIT 0,B
    BIT(0, Reg8(C), 2, 8)                               // 0x41 BIT 0,C
    BIT(0, Reg8(D), 2, 8)                               // 0x42 BIT 0,D
    BIT(0, Reg8(E), 2, 8)                               // 0x43 BIT 0,E
    BIT(0, Reg8(H), 2, 8)                               // 0x44 BIT 0,H
    BIT(0, Reg8(L), 2, 8)                               // 0x45 BIT 0,L
    BIT(0, Mem16(HL), 2, 12)                            // 0x46 BIT 0,(HL)
    BIT(0, Reg8(A), 2, 8)                               // 0x47 BIT 0,A
    BIT(1, Reg8(B), 2, 8)                               // 0x48 BIT 1,B
    BIT(1, Reg8(C), 2, 8)                               // 0x49 BIT 1,C
    BIT(1, Reg8(D), 2, 8)                               // 0x4A BIT 1,D
    BIT(1, Reg8(E), 2, 8)                               // 0x4B BIT 1,E
    BIT(1, Reg8(H), 2, 8)                               // 0x4C BIT 1,H
    BIT(1, Reg8(L), 2, 8)                               // 0x4D BIT 1,L
    BIT(1, Mem16(HL), 2, 12)                            // 0x4E BIT 1,(HL)
    BIT(1, Reg8(A), 2, 8)                               // 0x4F BIT 1,A
    BIT(2, Reg8(B), 2, 8)                               // 0x50 BIT 2,B
    BIT(2, Reg8(C), 2, 8)                               // 0x51 BIT 2,C
    BIT(2, Reg8(D), 2, 8)                               // 0x52 BIT 2,D
    BIT(2, Reg8(E), 2, 8)                               // 0x53 BIT 2,E
    BIT(2, Reg8(H), 2, 8)                               // 0x54 BIT 2,H
    BIT(2, Reg8(L), 2, 8)                               // 0x55 BIT 2,L
    BIT(2, Mem16(HL), 2, 12)                            // 0x56 BIT 2,(HL)
    BIT(2, Reg8(A), 2, 8)                               // 0x57 BIT 2,A
    BIT(3, Reg8(B), 2, 8)                               // 0x58 BIT 3,B
    BIT(3, Reg8(C), 2, 8)                               // 0x59 BIT 3,C
    BIT(3, Reg8(D), 2, 8)                               // 0x5A BIT 3,D
    BIT(3, Reg8(E), 2, 8)                               // 0x5B BIT 3,E
    BIT(3, Reg8(H), 2, 8)                               // 0x5C BIT 3,H
    BIT(3, Reg8(L), 2, 8)                               // 0x5D BIT 3,L
    BIT(3, Mem16(HL), 2, 12)                            // 0x5E BIT 3,(HL)
    BIT(3, Reg8(A), 2, 8)                               // 0x5F BIT 3,A
    BIT(4, Reg8(B), 2, 8)                               // 0x60 BIT 4,B
    BIT(4, Reg8(C), 2, 8)                               // 0x61 BIT 4,C
    BIT(4, Reg8(D), 2, 8)                               // 0x62 BIT 4,D
    BIT(4, Reg8(E), 2, 8)                               // 0x63 BIT 4,E
    BIT(4, Reg8(H), 2, 8)                               // 0x64 BIT 4,H
    BIT(4, Reg8(L), 2, 8)                               // 0x65 BIT 4,L
    BIT(4, Mem16(HL), 2, 12)                            // 0x66 BIT 4,(HL)
    BIT(4, Reg8(A), 2, 8)                               // 0x67 BIT 4,A
    BIT(5, Reg8(B), 2, 8)                               // 0x68 BIT 5,B
    BIT(5, Reg8(C), 2, 8)                               // 0x69 BIT 5,C
    BIT(5, Reg8(D), 2, 8)                               // 0x6A BIT 5,D
    BIT(5, Reg8(E), 2, 8)                               // 0x6B BIT 5,E
    BIT(5, Reg8(H), 2, 8)                               // 0x6C BIT 5,H
    BIT(5, Reg8(L), 2, 8)                               // 0x6D BIT 5,L
    BIT(5, Mem16(HL), 2, 12)                            // 0x6E BIT 5,(HL)
    BIT(5, Reg8(A), 2, 8)                               // 0x6F BIT 5,A
    BIT(6, Reg8(B), 2, 8)                               // 0x70 BIT 6,B
    BIT(6, Reg8(C), 2, 8)                               // 0x71 BIT 6,C
    BIT(6, Reg8(D), 2, 8)                               // 0x72 BIT 6,D
    BIT(6, Reg8(E), 2, 8)                               // 0x73 BIT 6,E
    BIT(6, Reg8(H), 2, 8)                               // 0x74 BIT 6,H
    BIT(6, Reg8(L), 2, 8)                               // 0x75 BIT 6,L
    BIT(6, Mem16(HL), 2, 12)                            // 0x76 BIT 6,(HL)
    BIT(6, Reg8(A), 2, 8)                               // 0x77 BIT 6,A
    BIT(7, Reg8(B), 2, 8)                               // 0x78 BIT 7,B
    BIT(7, Reg8(C), 2, 8)                               // 0x79 BIT 7,C
    BIT(7, Reg8(D), 2, 8)                               // 0x7A BIT 7,D
    BIT(7, Reg8(E), 2, 8)                               // 0x7B BIT 7,E
    BIT(7, Reg8(H), 2, 8)                               // 0x7C BIT 7,H
    BIT(7, Reg8(L), 2, 8)                               // 0x7D BIT 7,L
    BIT(7, Mem16(HL), 2, 12)                            // 0x7E BIT 7,(HL)
    BIT(7, Reg8(A), 2, 8)                               // 0x7F BIT 7,A
    RES(0, Reg8(B), 2, 8)                              // 0x80 RES 0,B
    RES(0, Reg8(C), 2, 8)                              // 0x81 RES 0,C
    RES(0, Reg8(D), 2, 8)                              // 0x82 RES 0,D
    RES(0, Reg8(E), 2, 8)                              // 0x83 RES 0,E
    RES(0, Reg8(H), 2, 8)                              // 0x84 RES 0,H
    RES(0, Reg8(L), 2, 8)                              // 0x85 RES 0,L
    RES(0, Mem16(HL), 2, 16)                           // 0x86 RES 0,(HL)
    RES(0, Reg8(A), 2, 8)                              // 0x87 RES 0,A
    RES(1, Reg8(B), 2, 8)                              // 0x88 RES 1,B
    RES(1, Reg8(C), 2, 8)                              // 0x89 RES 1,C
    RES(1, Reg8(D), 2, 8)                              // 0x8A RES 1,D
    RES(1, Reg8(E), 2, 8)                              // 0x8B RES 1,E
    RES(1, Reg8(H), 2, 8)                              // 0x8C RES 1,H
    RES(1, Reg8(L), 2, 8)                              // 0x8D RES 1,L
    RES(1, Mem16(HL), 2, 16)                           // 0x8E RES 1,(HL)
    RES(1, Reg8(A), 2, 8)                              // 0x8F RES 1,A
    RES(2, Reg8(B), 2, 8)                              // 0x90 RES 2,B
    RES(2, Reg8(C), 2, 8)                              // 0x91 RES 2,C
    RES(2, Reg8(D), 2, 8)                              // 0x92 RES 2,D
    RES(2, Reg8(E), 2, 8)                              // 0x93 RES 2,E
    RES(2, Reg8(H), 2, 8)                              // 0x94 RES 2,H
    RES(2, Reg8(L), 2, 8)                              // 0x95 RES 2,L
    RES(2, Mem16(HL), 2, 16)                           // 0x96 RES 2,(HL)
    RES(2, Reg8(A), 2, 8)                              // 0x97 RES 2,A
    RES(3, Reg8(B), 2, 8)                              // 0x98 RES 3,B
    RES(3, Reg8(C), 2, 8)                              // 0x99 RES 3,C
    RES(3, Reg8(D), 2, 8)                              // 0x9A RES 3,D
    RES(3, Reg8(E), 2, 8)                              // 0x9B RES 3,E
    RES(3, Reg8(H), 2, 8)                              // 0x9C RES 3,H
    RES(3, Reg8(L), 2, 8)                              // 0x9D RES 3,L
    RES(3, Mem16(HL), 2, 16)                           // 0x9E RES 3,(HL)
    RES(3, Reg8(A), 2, 8)                              // 0x9F RES 3,A
    RES(4, Reg8(B), 2, 8)                              // 0xA0 RES 4,B
    RES(4, Reg8(C), 2, 8)                              // 0xA1 RES 4,C
    RES(4, Reg8(D), 2, 8)                              // 0xA2 RES 4,D
    RES(4, Reg8(E), 2, 8)                              // 0xA3 RES 4,E
    RES(4, Reg8(H), 2, 8)                              // 0xA4 RES 4,H
    RES(4, Reg8(L), 2, 8)                              // 0xA5 RES 4,L
    RES(4, Mem16(HL), 2, 16)                           // 0xA6 RES 4,(HL)
    RES(4, Reg8(A), 2, 8)                              // 0xA7 RES 4,A
    RES(5, Reg8(B), 2, 8)                              // 0xA8 RES 5,B
    RES(5, Reg8(C), 2, 8)                              // 0xA9 RES 5,C
    RES(5, Reg8(D), 2, 8)                              // 0xAA RES 5,D
    RES(5, Reg8(E), 2, 8)                              // 0xAB RES 5,E
    RES(5, Reg8(H), 2, 8)                              // 0xAC RES 5,H
    RES(5, Reg8(L), 2, 8)                              // 0xAD RES 5,L
    RES(5, Mem16(HL), 2, 16)                           // 0xAE RES 5,(HL)
    RES(5, Reg8(A), 2, 8)                              // 0xAF RES 5,A
    RES(6, Reg8(B), 2, 8)                              // 0xB0 RES 6,B
    RES(6, Reg8(C), 2, 8)                              // 0xB1 RES 6,C
    RES(6, Reg8(D), 2, 8)                              // 0xB2 RES 6,D
    RES(6, Reg8(E), 2, 8)                              // 0xB3 RES 6,E
    RES(6, Reg8(H), 2, 8)                              // 0xB4 RES 6,H
    RES(6, Reg8(L), 2, 8)                              // 0xB5 RES 6,L
    RES(6, Mem16(HL), 2, 16)                           // 0xB6 RES 6,(HL)
    RES(6, Reg8(A), 2, 8)                              // 0xB7 RES 6,A
    RES(7, Reg8(B), 2, 8)                              // 0xB8 RES 7,B
    RES(7, Reg8(C), 2, 8)                              // 0xB9 RES 7,C
    RES(7, Reg8(D), 2, 8)                              // 0xBA RES 7,D
    RES(7, Reg8(E), 2, 8)                              // 0xBB RES 7,E
    RES(7, Reg8(H), 2, 8)                              // 0xBC RES 7,H
    RES(7, Reg8(L), 2, 8)                              // 0xBD RES 7,L
    RES(7, Mem16(HL), 2, 16)                           // 0xBE RES 7,(HL)
    RES(7, Reg8(A), 2, 8)                              // 0xBF RES 7,A
    SET(0, Reg8(B), 2, 8)                                // 0xC0 SET 0,B
    SET(0, Reg8(C), 2, 8)                                // 0xC1 SET 0,C
    SET(0, Reg8(D), 2, 8)                                // 0xC2 SET 0,D
    SET(0, Reg8(E), 2, 8)                                // 0xC3 SET 0,E
    SET(0, Reg8(H), 2, 8)                                // 0xC4 SET 0,H
    SET(0, Reg8(L), 2, 8)                                // 0xC5 SET 0,L
    SET(0, Mem16(HL), 2, 16)                             // 0xC6 SET 0,(HL)
    SET(0, Reg8(A), 2, 8)                                // 0xC7 SET 0,A
    SET(1, Reg8(B), 2, 8)                                // 0xC8 SET 1,B
    SET(1, Reg8(C), 2, 8)                                // 0xC9 SET 1,C
    SET(1, Reg8(D), 2, 8)                                // 0xCA SET 1,D
    SET(1, Reg8(E), 2, 8)                                // 0xCB SET 1,E
    SET(1, Reg8(H), 2, 8)                                // 0xCC SET 1,H
    SET(1, Reg8(L), 2, 8)                                // 0xCD SET 1,L
    SET(1, Mem16(HL), 2, 16)                             // 0xCE SET 1,(HL)
    SET(1, Reg8(A), 2, 8)                                // 0xCF SET 1,A
    SET(2, Reg8(B), 2, 8)                                // 0xD0 SET 2,B
    SET(2, Reg8(C), 2, 8)                                // 0xD1 SET 2,C
    SET(2, Reg8(D), 2, 8)                                // 0xD2 SET 2,D
    SET(2, Reg8(E), 2, 8)                                // 0xD3 SET 2,E
    SET(2, Reg8(H), 2, 8)                                // 0xD4 SET 2,H
    SET(2, Reg8(L), 2, 8)                                // 0xD5 SET 2,L
    SET(2, Mem16(HL), 2, 16)                             // 0xD6 SET 2,(HL)
    SET(2, Reg8(A), 2, 8)                                // 0xD7 SET 2,A
    SET(3, Reg8(B), 2, 8)                                // 0xD8 SET 3,B
    SET(3, Reg8(C), 2, 8)                                // 0xD9 SET 3,C
    SET(3, Reg8(D), 2, 8)                                // 0xDA SET 3,D
    SET(3, Reg8(E), 2, 8)                                // 0xDB SET 3,E
    SET(3, Reg8(H), 2, 8)                                // 0xDC SET 3,H
    SET(3, Reg8(L), 2, 8)                                // 0xDD SET 3,L
    SET(3, Mem16(HL), 2, 16)                             // 0xDE SET 3,(HL)
    SET(3, Reg8(A), 2, 8)                                // 0xDF SET 3,A
    SET(4, Reg8(B), 2, 8)                                // 0xE0 SET 4,B
    SET(4, Reg8(C), 2, 8)                                // 0xE1 SET 4,C
    SET(4, Reg8(D), 2, 8)                                // 0xE2 SET 4,D
    SET(4, Reg8(E), 2, 8)                                // 0xE3 SET 4,E
    SET(4, Reg8(H), 2, 8)                                // 0xE4 SET 4,H
    SET(4, Reg8(L), 2, 8)                                // 0xE5 SET 4,L
    SET(4, Mem16(HL), 2, 16)                             // 0xE6 SET 4,(HL)
    SET(4, Reg8(A), 2, 8)                                // 0xE7 SET 4,A
    SET(5, Reg8(B), 2, 8)                                // 0xE8 SET 5,B
    SET(5, Reg8(C), 2, 8)                                // 0xE9 SET 5,C
    SET(5, Reg8(D), 2, 8)                                // 0xEA SET 5,D
    SET(5, Reg8(E), 2, 8)                                // 0xEB SET 5,E
    SET(5, Reg8(H), 2, 8)                                // 0xEC SET 5,H
    SET(5, Reg8(L), 2, 8)                                // 0xED SET 5,L
    SET(5, Mem16(HL), 2, 16)                             // 0xEE SET 5,(HL)
    SET(5, Reg8(A), 2, 8)                                // 0xEF SET 5,A
    SET(6, Reg8(B), 2, 8)                                // 0xF0 SET 6,B
    SET(6, Reg8(C), 2, 8)                                // 0xF1 SET 6,C
    SET(6, Reg8(D), 2, 8)                                // 0xF2 SET 6,D
    SET(6, Reg8(E), 2, 8)                                // 0xF3 SET 6,E
    SET(6, Reg8(H), 2, 8)                                // 0xF4 SET 6,H
    SET(6, Reg8(L), 2, 8)                                // 0xF5 SET 6,L
    SET(6, Mem16(HL), 2, 16)                             // 0xF6 SET 6,(HL)
    SET(6, Reg8(A), 2, 8)                                // 0xF7 SET 6,A
    SET(7, Reg8(B), 2, 8)                                // 0xF8 SET 7,B
    SET(7, Reg8(C), 2, 8)                                // 0xF9 SET 7,C
    SET(7, Reg8(D), 2, 8)                                // 0xFA SET 7,D
    SET(7, Reg8(E), 2, 8)                                // 0xFB SET 7,E
    SET(7, Reg8(H), 2, 8)                                // 0xFC SET 7,H
    SET(7, Reg8(L), 2, 8)                                // 0xFD SET 7,L
    SET(7, Mem16(HL), 2, 16)                             // 0xFE SET 7,(HL)
    SET(7, Reg8(A), 2, 8)                                // 0xFF SET 7,A
};

}
