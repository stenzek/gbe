#include "cpu.h"
#include "YBaseLib/Memory.h"
#include "YBaseLib/Assert.h"
#include "YBaseLib/String.h"
#include "YBaseLib/Log.h"
Log_SetChannel(CPU);

// http://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html
// http://gameboy.mongenel.com/dmg/opcodes.html
// http://imrannazar.com/GameBoy-Emulation-in-JavaScript:-The-CPU
// http://stackoverflow.com/questions/8034566/overflow-and-carry-flags-on-z80

#define Operand(mode, reg) { mode, reg }
#define NoOperand() { Instruction::NumAddressModes }
#define Imm8() Operand(Instruction::AddressMode_Imm8, NumReg8)
#define Imm16() Operand(Instruction::AddressMode_Imm16, NumReg8)
#define Reg8(reg) Operand(Instruction::AddressMode_Reg8, (Reg8)reg)
#define Reg16(reg) Operand(Instruction::AddressMode_Reg16, (Reg8)reg)
#define Mem8(reg) Operand(Instruction::AddressMode_Mem8, (Reg8)reg)
#define Mem16(reg) Operand(Instruction::AddressMode_Mem16, (Reg8)reg)
#define Addr8() Operand(Instruction::AddressMode_Addr8, NumReg8)
#define Addr16() Operand(Instruction::AddressMode_Addr16, NumReg8)
#define AF Reg16_AF
#define BC Reg16_BC
#define DE Reg16_DE
#define HL Reg16_HL
#define SP Reg16_SP
#define PC Reg16_PC
#define A Reg8_A
#define F Reg8_F
#define B Reg8_B
#define C Reg8_C
#define D Reg8_D
#define E Reg8_E
#define H Reg8_H
#define L Reg8_L
#define Always Instruction::Predicate_Always
#define Zero Instruction::Predicate_Zero
#define Carry Instruction::Predicate_Carry
#define NotZero Instruction::Predicate_NotZero
#define NotCarry Instruction::Predicate_NotCarry
#define FromInterrupt Instruction::Predicate_FromInterrupt
#define IncrementAddress Instruction::LoadStoreAction_IncrementAddress
#define DecrementAddress Instruction::LoadStoreAction_DecrementAddress
#define WithCarry Instruction::CarryAction_With
#define WithoutCarry Instruction::CarryAction_Ignore
#define Left Instruction::RotateDirection_Left
#define Right Instruction::RotateDirection_Left
#define SCF Instruction::Untyped_SCF
#define CCF Instruction::Untyped_CCF

#define Stub(length, cycles) { Instruction::Type_Stub, NoOperand(), NoOperand(), length, cycles },
#define Nop(length, cycles) { Instruction::Type_Nop, NoOperand(), NoOperand(), length, cycles },
#define Load(dst, src, length, cycles) { Instruction::Type_Load, dst, src, length, cycles },
#define Store(dst, src, length, cycles) { Instruction::Type_Store, dst, src, length, cycles },
#define Move(dst, src, length, cycles)  { Instruction::Type_Move, dst, src, length, cycles },
#define LoadAnd(action, dst, src, length, cycles) { Instruction::Type_Load, dst, src, length, cycles, action },
#define StoreAnd(action, dst, src, length, cycles) { Instruction::Type_Store, dst, src, length, cycles, action },
#define ReadIOReg(dst, src, length, cycles) { Instruction::Type_ReadIOReg, dst, src, length, cycles },
#define WriteIOReg(dst, src, length, cycles) { Instruction::Type_WriteIOReg, dst, src, length, cycles },
#define Increment(dst, length, cycles) { Instruction::Type_Increment, dst, NoOperand(), length, cycles },
#define Decrement(dst, length, cycles) { Instruction::Type_Decrement, dst, NoOperand(), length, cycles },
#define Add(carry, dst, src, length, cycles) { Instruction::Type_Add, dst, src, length, cycles, (Instruction::LoadStoreAction)carry },
#define Sub(carry, dst, src, length, cycles) { Instruction::Type_Sub, dst, src, length, cycles, (Instruction::LoadStoreAction)carry },
#define Rotate(direction, carry, dst, length, cycles) { Instruction::Type_Rotate, dst, { Instruction::NumAddressModes, (Reg8)direction }, length, carry, (Instruction::LoadStoreAction)carry }, 
#define And(dst, src, length, cycles) { Instruction::Type_And, dst, src, length, cycles },
#define Or(dst, src, length, cycles) { Instruction::Type_Or, dst, src, length, cycles },
#define Xor(dst, src, length, cycles) { Instruction::Type_Xor, dst, src, length, cycles },
#define Cmp(dst, src, length, cycles) { Instruction::Type_Cmp, dst, src, length, cycles },
#define Not(dst, src, length, cycles) { Instruction::Type_Not, dst, src, length, cycles },
#define Swap(dst, src, length, cycles) { Instruction::Type_Swap, dst, src, length, cycles },
#define Bit(bitnum, src, length, cycles) { Instruction::Type_Bit, NoOperand(), src, length, cycles, (Instruction::LoadStoreAction)bitnum },
#define JumpRelative(predicate, length, cycles, cycles_skipped) { Instruction::Type_JumpRelative, Imm8(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define JumpAbsolute(predicate, length, cycles, cycles_skipped) { Instruction::Type_JumpAbsolute, Imm16(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define Call(predicate, length, cycles, cycles_skipped) { Instruction::Type_Call, Imm16(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define Return(predicate, length, cycles, cycles_skipped) { Instruction::Type_Return, NoOperand(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)predicate, cycles_skipped },
#define Push(src, length, cycles) { Instruction::Type_Push, NoOperand(), src, length, cycles },
#define Pop(dst, length, cycles) { Instruction::Type_Pop, dst, NoOperand(), length, cycles },
#define Restart(vector, length, cycles) { Instruction::Type_Restart, { Instruction::NumAddressModes, (Reg8)vector }, NoOperand(), length, cycles },
#define EnableInterrupts(state, length, cycles) { Instruction::Type_EnableInterrupts, NoOperand(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)state },
#define Untyped(type, length, cycles) { Instruction::Type_Untyped, NoOperand(), NoOperand(), length, cycles, (Instruction::LoadStoreAction)type },
#define Prefix() { Instruction::Type_Prefix },

const CPU::Instruction CPU::instructions[256] =
{
    // 0x00 - 0x0F
    Nop(1, 4)                                               // 0x00 NOP
    Load(Reg16(BC), Imm16(), 3, 12)                         // 0x01 LD BC, d16
    Store(Mem16(BC), Reg8(A), 1, 8)                         // 0x02 LD (BC), A
    Increment(Reg16(BC), 1, 8)                              // 0x03 INC BC
    Increment(Reg8(B), 1, 4)                                // 0x04 INC B
    Decrement(Reg8(B), 1, 4)                                // 0x05 DEC B
    Load(Reg8(B), Imm8(), 2, 8)                             // 0x06 LD B, d8
    Stub(1, 4)                                              // 0x07 RLCA
    Store(Addr16(), Reg16(SP), 3, 20)                       // 0x08 LD (a16), SP
    Add(WithoutCarry, Reg16(HL), Reg16(BC), 1, 8)           // 0x09 ADD HL, BC
    Load(Reg8(A), Mem16(BC), 1, 8)                          // 0x0A LD A, (BC)
    Decrement(Reg16(BC), 1, 8)                              // 0x0B DEC BC
    Increment(Reg8(C), 1, 4)                                // 0x0C INC C
    Decrement(Reg8(C), 1, 4)                                // 0x0D DEC C
    Load(Reg8(C), Imm8(), 2, 8)                             // 0x0E LD C, d8
    Stub(1, 4)                                              // 0x0F RRCA

    // 0x10 - 0x1F
    Stub(2, 4)                                              // 0x10 STOP 0
    Load(Reg16(DE), Imm16(), 3, 12)                         // 0x11 LD DE, d16
    Store(Mem16(DE), Reg8(A), 1, 8)                         // 0x12 LD (DE), A
    Increment(Reg16(DE), 1, 8)                              // 0x13 INC DE
    Increment(Reg8(D), 1, 4)                                // 0x14 INC D
    Decrement(Reg8(D), 1, 4)                                // 0x15 DEC D
    Load(Reg8(D), Imm8(), 2, 8)                             // 0x16 LD D, d8
    Rotate(Left, WithoutCarry, Reg8(A), 1, 4)               // 0x17 RLA
    JumpRelative(Always, 2, 12, 12)                         // 0x18 JR r8
    Add(WithoutCarry, Reg16(HL), Reg16(DE), 1, 8)           // 0x19 ADD HL, DE
    Load(Reg8(A), Mem16(DE), 1, 8)                          // 0x1A LD A, (DE)
    Decrement(Reg16(DE), 1, 8)                              // 0x1B DEC DE
    Increment(Reg8(E), 1, 4)                                // 0x1C INC E
    Decrement(Reg8(E), 1, 4)                                // 0x1D DEC E
    Load(Reg8(E), Imm8(), 2, 8)                             // 0x1E LD E, d8
    Stub(0, 0)                                              // 0x1F RRA

    // 0x20 - 0x2F
    JumpRelative(NotZero, 2, 12, 8)                         // 0x20 JR NZ, r8
    Load(Reg16(HL), Imm16(), 3, 12)                         // 0x21 LD HL, d16
    StoreAnd(IncrementAddress, Mem16(HL), Reg8(A), 1, 8)    // 0x22 LDI (HL), A
    Increment(Reg16(HL), 1, 8)                              // 0x23 INC HL
    Increment(Reg8(H), 1, 4)                                // 0x24 INC H
    Decrement(Reg8(H), 1, 4)                                // 0x25 DEC H
    Load(Reg8(H), Imm8(), 2, 8)                             // 0x26 LD H, d8
    Stub(1, 4)                                              // 0x27 DAA
    JumpRelative(Zero, 2, 12, 8)                            // 0x28 JR Z, r8
    Add(WithoutCarry, Reg16(HL), Reg16(HL), 1, 8)           // 0x29 ADD HL, HL
    LoadAnd(IncrementAddress, Reg8(A), Mem16(HL), 1, 8)     // 0x2A LDI A, (HL)
    Decrement(Reg16(HL), 1, 8)                              // 0x2B DEC HL
    Increment(Reg8(L), 1, 4)                                // 0x2C INC L
    Decrement(Reg8(L), 1, 4)                                // 0x2D DEC L
    Load(Reg8(L), Imm8(), 2, 8)                             // 0x2E LD L, d8
    Not(Reg8(A), Reg8(A), 1, 4)                             // 0x2F CPL

    // 0x30 - 0x3F
    JumpRelative(NotCarry, 2, 12, 8)                        // 0x30 JR NC, r8
    Load(Reg16(SP), Imm16(), 3, 12)                         // 0x31 LD SP, d16
    StoreAnd(DecrementAddress, Mem16(HL), Reg8(A), 1, 8)    // 0x32 LDD (HL), A
    Increment(Reg16(SP), 1, 8)                              // 0x33 INC SP
    Increment(Mem16(HL), 1, 12)                             // 0x34 INC (HL)
    Decrement(Mem16(HL), 1, 12)                             // 0x35 DEC (HL)
    Store(Mem16(HL), Imm8(), 2, 12)                         // 0x36 LD (HL), d8
    Untyped(SCF, 1, 4)                                      // 0x37 SCF
    JumpRelative(Carry, 2, 12, 8)                           // 0x38 JR C, r8
    Add(WithoutCarry, Reg16(HL), Reg16(SP), 1, 8)           // 0x39 ADD HL, SP
    LoadAnd(DecrementAddress, Reg8(A), Reg16(HL), 1, 8)     // 0x3A LDD A, (HL)
    Decrement(Reg16(SP), 1, 8)                              // 0x3B DEC SP
    Increment(Reg8(A), 1, 4)                                // 0x3C INC A
    Decrement(Reg8(A), 1, 4)                                // 0x3D DEC A
    Load(Reg8(A), Imm8(), 2, 8)                             // 0x3E LD A, d8
    Untyped(CCF, 1, 4)                                      // 0x3F CCF
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
    Load(Reg8(E), Reg16(HL), 1, 8)                          // 0x5E LD E, (HL)
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
    Move(Reg8(L), Reg8(B), 1, 4)                            // 0x6F LD L, A
    Store(Mem16(HL), Reg8(B), 1, 8)                         // 0x70 LD (HL), B
    Store(Mem16(HL), Reg8(C), 1, 8)                         // 0x71 LD (HL), C
    Store(Mem16(HL), Reg8(D), 1, 8)                         // 0x72 LD (HL), D
    Store(Mem16(HL), Reg8(E), 1, 8)                         // 0x73 LD (HL), E
    Store(Mem16(HL), Reg8(H), 1, 8)                         // 0x74 LD (HL), H
    Store(Mem16(HL), Reg8(L), 1, 8)                         // 0x75 LD (HL), L
    Stub(1, 4)                                              // 0x76 HALT
    Store(Mem16(HL), Reg8(A), 1, 8)                         // 0x77 LD (HL), A
    Move(Reg8(A), Reg8(B), 1, 4)                            // 0x78 LD A, B
    Move(Reg8(A), Reg8(C), 1, 4)                            // 0x79 LD A, C
    Move(Reg8(A), Reg8(D), 1, 4)                            // 0x7A LD A, D
    Move(Reg8(A), Reg8(E), 1, 4)                            // 0x7B LD A, E
    Move(Reg8(A), Reg8(H), 1, 4)                            // 0x7C LD A, H
    Move(Reg8(A), Reg8(L), 1, 4)                            // 0x7D LD A, L
    Load(Reg8(A), Mem16(HL), 1, 8)                          // 0x7E LD A, (HL)
    Move(Reg8(A), Reg8(A), 1, 4)                            // 0x7F LD A, A
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x80 ADD A, B
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x81 ADD A, C
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x82 ADD A, D
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x83 ADD A, E
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x84 ADD A, H
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x85 ADD A, L
    Add(WithoutCarry, Reg8(A), Mem16(HL), 1, 8)             // 0x86 ADD A, (HL)
    Add(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x87 ADD A, A
    Add(WithCarry, Reg8(A), Reg8(B), 1, 4)                  // 0x88 ADC A, B
    Add(WithCarry, Reg8(A), Reg8(C), 1, 4)                  // 0x89 ADC A, C
    Add(WithCarry, Reg8(A), Reg8(D), 1, 4)                  // 0x8A ADC A, D
    Add(WithCarry, Reg8(A), Reg8(E), 1, 4)                  // 0x8B ADC A, E
    Add(WithCarry, Reg8(A), Reg8(H), 1, 4)                  // 0x8C ADC A, H
    Add(WithCarry, Reg8(A), Reg8(L), 1, 4)                  // 0x8D ADC A, L
    Add(WithCarry, Reg8(A), Mem16(HL), 1, 8)                // 0x8E ADC A, (HL)
    Add(WithCarry, Reg8(A), Reg8(A), 1, 4)                  // 0x8F ADC A, A
    Sub(WithoutCarry, Reg8(A), Reg8(B), 1, 4)               // 0x90 SUB B
    Sub(WithoutCarry, Reg8(A), Reg8(C), 1, 4)               // 0x91 SUB C
    Sub(WithoutCarry, Reg8(A), Reg8(D), 1, 4)               // 0x92 SUB D
    Sub(WithoutCarry, Reg8(A), Reg8(E), 1, 4)               // 0x93 SUB E
    Sub(WithoutCarry, Reg8(A), Reg8(H), 1, 4)               // 0x94 SUB H
    Sub(WithoutCarry, Reg8(A), Reg8(L), 1, 4)               // 0x95 SUB L
    Sub(WithoutCarry, Reg8(A), Mem16(HL), 1, 8)             // 0x96 SUB (HL)
    Sub(WithoutCarry, Reg8(A), Reg8(A), 1, 4)               // 0x97 SUB A
    Sub(WithCarry, Reg8(A), Reg8(B), 1, 4)                  // 0x98 SBC A, B
    Sub(WithCarry, Reg8(A), Reg8(C), 1, 4)                  // 0x99 SBC A, C
    Sub(WithCarry, Reg8(A), Reg8(D), 1, 4)                  // 0x9A SBC A, D
    Sub(WithCarry, Reg8(A), Reg8(E), 1, 4)                  // 0x9B SBC A, E
    Sub(WithCarry, Reg8(A), Reg8(H), 1, 4)                  // 0x9C SBC A, H
    Sub(WithCarry, Reg8(A), Reg8(L), 1, 4)                  // 0x9D SBC A, L
    Sub(WithCarry, Reg8(A), Mem16(HL), 1, 8)                // 0x9E SBC A, (HL)
    Sub(WithCarry, Reg8(A), Reg8(A), 1, 4)                  // 0x9F SBC A, A
    And(Reg8(A), Reg8(B), 1, 4)                             // 0xA0 AND B
    And(Reg8(A), Reg8(C), 1, 4)                             // 0xA1 AND C
    And(Reg8(A), Reg8(D), 1, 4)                             // 0xA2 AND D
    And(Reg8(A), Reg8(E), 1, 4)                             // 0xA3 AND E
    And(Reg8(A), Reg8(H), 1, 4)                             // 0xA4 AND H
    And(Reg8(A), Reg8(L), 1, 4)                             // 0xA5 AND L
    And(Reg8(A), Mem16(HL), 1, 8)                           // 0xA6 AND (HL)
    And(Reg8(A), Reg8(A), 1, 4)                             // 0xA7 AND A
    Xor(Reg8(A), Reg8(B), 1, 4)                             // 0xA8 XOR B
    Xor(Reg8(A), Reg8(C), 1, 4)                             // 0xA9 XOR C
    Xor(Reg8(A), Reg8(D), 1, 4)                             // 0xAA XOR D
    Xor(Reg8(A), Reg8(E), 1, 4)                             // 0xAB XOR E
    Xor(Reg8(A), Reg8(H), 1, 4)                             // 0xAC XOR H
    Xor(Reg8(A), Reg8(L), 1, 4)                             // 0xAD XOR L
    Xor(Reg8(A), Mem16(HL), 1, 8)                           // 0xAE XOR (HL)
    Xor(Reg8(A), Reg8(A), 1, 4)                             // 0xAF XOR A
    Or(Reg8(A), Reg8(B), 1, 4)                              // 0xB0 OR B
    Or(Reg8(A), Reg8(C), 1, 4)                              // 0xB1 OR C
    Or(Reg8(A), Reg8(D), 1, 4)                              // 0xB2 OR D
    Or(Reg8(A), Reg8(E), 1, 4)                              // 0xB3 OR E
    Or(Reg8(A), Reg8(H), 1, 4)                              // 0xB4 OR H
    Or(Reg8(A), Reg8(L), 1, 4)                              // 0xB5 OR L
    Or(Reg8(A), Mem16(HL), 1, 8)                            // 0xB6 OR (HL)
    Or(Reg8(A), Reg8(A), 1, 4)                              // 0xB7 OR A
    Cmp(Reg8(A), Reg8(B), 1, 4)                             // 0xB8 CP B
    Cmp(Reg8(A), Reg8(C), 1, 4)                             // 0xB9 CP C
    Cmp(Reg8(A), Reg8(D), 1, 4)                             // 0xBA CP D
    Cmp(Reg8(A), Reg8(E), 1, 4)                             // 0xBB CP E
    Cmp(Reg8(A), Reg8(H), 1, 4)                             // 0xBC CP H
    Cmp(Reg8(A), Reg8(L), 1, 4)                             // 0xBD CP L
    Cmp(Reg8(A), Mem16(HL), 1, 8)                           // 0xBE CP (HL)
    Cmp(Reg8(A), Reg8(A), 1, 4)                             // 0xBF CP A
    Return(NotZero, 1, 20, 8)                               // 0xC0 RET NZ
    Pop(Reg16(BC), 1, 12)                                   // 0xC1 POP BC
    JumpAbsolute(NotZero, 3, 16, 12)                        // 0xC2 JP NZ, a16
    JumpAbsolute(Always, 3, 16, 16)                         // 0xC3 JP a16
    Call(NotZero, 3, 24, 12)                                // 0xC4 CALL NZ, a16
    Push(Reg16(BC), 1, 16)                                  // 0xC5 PUSH BC
    Stub(2, 0)                                              // 0xC6 ADD A, d8
    Restart(0, 1, 16)                                       // 0xC7 RST 00H
    Return(Zero, 1, 20, 8)                                  // 0xC8 RET Z
    Return(Always, 1, 16, 16)                               // 0xC9 RET
    JumpAbsolute(Zero, 3, 16, 12)                           // 0xCA JP Z, a16
    Prefix()                                                // 0xCB PREFIX CB
    Call(Zero, 3, 24, 12)                                   // 0xCC CALL Z, a16
    Call(Always, 3, 24, 12)                                 // 0xCD CALL a16
    Stub(2, 0)                                              // 0xCE ADC A, d8
    Restart(0x08, 1, 16)                                    // 0xCF RST 08H
    Return(NotCarry, 1, 20, 8)                              // 0xD0 RET NC
    Pop(Reg16(DE), 1, 12)                                   // 0xD1 POP DE
    Stub(3, 0)                                              // 0xD2 JP NC, a16
    Stub(1, 0)                                              // 0xD3
    Call(NotCarry, 3, 24, 12)                               // 0xD4 CALL NC, a16
    Push(Reg16(DE), 1, 16)                                  // 0xD5 PUSH DE
    Stub(2, 0)                                              // 0xD6 SUB d8
    Restart(0x10, 1, 16)                                    // 0xD7 RST 10H
    Stub(1, 0)                                              // 0xD8 RET C
    Return(FromInterrupt, 1, 16, 16)                        // 0xD9 RETI
    Stub(3, 0)                                              // 0xDA JP C, a16
    Stub(1, 0)                                              // 0xDB
    Call(Carry, 3, 24, 12)                                  // 0xDC CALL C, a16
    Stub(1, 0)                                              // 0xDD
    Stub(2, 0)                                              // 0xDE SBC A, d8
    Restart(0x18, 1, 16)                                    // 0xDF RST 18H
    WriteIOReg(Imm8(), Reg8(A), 2, 12)                      // 0xE0 LDH (a8), A
    Pop(Reg16(HL), 1, 12)                                   // 0xE1 POP HL
    WriteIOReg(Reg8(C), Reg8(A), 1, 8)                      // 0xE2 LD (C), A
    Stub(1, 0)                                              // 0xE3
    Stub(1, 0)                                              // 0xE4
    Push(Reg16(HL), 1, 16)                                  // 0xE5 PUSH HL
    And(Reg8(A), Imm8(), 2, 8)                              // 0xE6 AND d8
    Restart(0x20, 1, 16)                                    // 0xE7 RST 20H
    Stub(2, 0)                                              // 0xE8 ADD SP, r8
    Stub(1, 0)                                              // 0xE9 JP (HL)
    Store(Addr16(), Reg8(A), 3, 16)                         // 0xEA LD (a16), A
    Stub(1, 0)                                              // 0xEB
    Stub(1, 0)                                              // 0xEC
    Stub(1, 0)                                              // 0xED
    Stub(2, 0)                                              // 0xEE XOR d8
    Restart(0x28, 1, 16)                                    // 0xEF RST 28H
    ReadIOReg(Reg8(A), Imm8(), 2, 12)                       // 0xF0 LDH A, (a8)
    Pop(Reg16(AF), 1, 12)                                   // 0xF1 POP AF
    ReadIOReg(Reg8(A), Reg8(C), 1, 8)                       // 0xF2 LD A, (C)
    EnableInterrupts(false, 1, 4)                           // 0xF3 DI
    Stub(1, 0)                                              // 0xF4
    Push(Reg16(AF), 1, 16)                                  // 0xF5 PUSH AF
    Stub(2, 0)                                              // 0xF6 OR d8
    Restart(0x30, 1, 16)                                    // 0xF7 RST 30H
    Stub(2, 0)                                              // 0xF8 LD HL, SP+r8
    Stub(1, 0)                                              // 0xF9 LD SP, HL
    Load(Reg8(A), Addr16(), 3, 16)                          // 0xFA LD A, (a16)
    EnableInterrupts(true, 1, 4)                            // 0xFB EI
    Stub(1, 0)                                              // 0xFC
    Stub(1, 0)                                              // 0xFD
    Cmp(Reg8(A), Imm8(), 2, 8)                              // 0xFE CP d8
    Restart(0x38, 1, 16)                                    // 0xFF RST 38H
};

const CPU::Instruction CPU::cb_instructions[256] =
{
    Rotate(Left, WithCarry, Reg8(B), 2, 8)                  // 0x00 RLC B
    Rotate(Left, WithCarry, Reg8(C), 2, 8)                  // 0x01 RLC C
    Rotate(Left, WithCarry, Reg8(D), 2, 8)                  // 0x02 RLC D
    Rotate(Left, WithCarry, Reg8(E), 2, 8)                  // 0x03 RLC E
    Rotate(Left, WithCarry, Reg8(H), 2, 8)                  // 0x04 RLC H
    Rotate(Left, WithCarry, Reg8(L), 2, 8)                  // 0x05 RLC L
    Rotate(Left, WithCarry, Mem16(B), 2, 16)                // 0x06 RLC (HL)
    Rotate(Left, WithCarry, Reg8(A), 2, 8)                  // 0x07 RLC A
    Rotate(Right, WithCarry, Reg8(B), 2, 8)                 // 0x08 RRC B
    Rotate(Right, WithCarry, Reg8(C), 2, 8)                 // 0x09 RRC C
    Rotate(Right, WithCarry, Reg8(D), 2, 8)                 // 0x0A RRC D
    Rotate(Right, WithCarry, Reg8(E), 2, 8)                 // 0x0B RRC E
    Rotate(Right, WithCarry, Reg8(H), 2, 8)                 // 0x0C RRC H
    Rotate(Right, WithCarry, Reg8(L), 2, 8)                 // 0x0D RRC L
    Rotate(Right, WithCarry, Mem16(HL), 2, 16)              // 0x0E RRC (HL)
    Rotate(Right, WithCarry, Reg8(A), 2, 8)                 // 0x0F RRC A
    Rotate(Left, WithoutCarry, Reg8(B), 2, 8)               // 0x10 RL B
    Rotate(Left, WithoutCarry, Reg8(C), 2, 8)               // 0x11 RL C
    Rotate(Left, WithoutCarry, Reg8(D), 2, 8)               // 0x12 RL D
    Rotate(Left, WithoutCarry, Reg8(E), 2, 8)               // 0x13 RL E
    Rotate(Left, WithoutCarry, Reg8(H), 2, 8)               // 0x14 RL H
    Rotate(Left, WithoutCarry, Reg8(L), 2, 8)               // 0x15 RL L
    Rotate(Left, WithoutCarry, Mem16(HL), 2, 16)            // 0x16 RL (HL)
    Rotate(Right, WithoutCarry, Reg8(A), 2, 8)              // 0x17 RL A
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x18 RR B
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x19 RR C
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x1A RR D
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x1B RR E
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x1C RR H
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x1D RR L
    Rotate(Right, WithoutCarry, Mem16(HL), 2, 16)           // 0x1E RR (HL)
    Rotate(Right, WithoutCarry, Reg8(B), 2, 8)              // 0x1F RR A
    Stub(2, 8)                                              // 0x20 SLA B
    Stub(2, 8)                                              // 0x21 SLA C
    Stub(2, 8)                                              // 0x22 SLA D
    Stub(2, 8)                                              // 0x23 SLA E
    Stub(2, 8)                                              // 0x24 SLA H
    Stub(2, 8)                                              // 0x25 SLA L
    Stub(2, 16)                                             // 0x26 SLA (HL)
    Stub(2, 8)                                              // 0x27 SLA A
    Stub(2, 8)                                              // 0x28 SRA B
    Stub(2, 8)                                              // 0x29 SRA C
    Stub(2, 8)                                              // 0x2A SRA D
    Stub(2, 8)                                              // 0x2B SRA E
    Stub(2, 8)                                              // 0x2C SRA H
    Stub(2, 8)                                              // 0x2D SRA L
    Stub(2, 16)                                             // 0x2E SRA (HL)
    Stub(2, 8)                                              // 0x2F SRA A
    Stub(2, 8)                                              // 0x30 SWAP B
    Stub(2, 8)                                              // 0x31 SWAP C
    Stub(2, 8)                                              // 0x32 SWAP D
    Stub(2, 8)                                              // 0x33 SWAP E
    Stub(2, 8)                                              // 0x34 SWAP H
    Stub(2, 8)                                              // 0x35 SWAP L
    Stub(2, 16)                                             // 0x36 SWAP (HL)
    Swap(Reg8(A), Reg8(A), 2, 8)                            // 0x37 SWAP A
    Stub(2, 8)                                              // 0x38 SRL B
    Stub(2, 8)                                              // 0x39 SRL C
    Stub(2, 8)                                              // 0x3A SRL D
    Stub(2, 8)                                              // 0x3B SRL E
    Stub(2, 8)                                              // 0x3C SRL H
    Stub(2, 8)                                              // 0x3D SRL L
    Stub(2, 16)                                             // 0x3E SRL (HL)
    Stub(2, 8)                                              // 0x3F SRL A
    Bit(0, Reg8(B), 2, 8)                                   // 0x40 BIT 0,B
    Stub(2, 8)                                              // 0x41 BIT 0,C
    Stub(2, 8)                                              // 0x42 BIT 0,D
    Stub(2, 8)                                              // 0x43 BIT 0,E
    Stub(2, 8)                                              // 0x44 BIT 0,H
    Stub(2, 8)                                              // 0x45 BIT 0,L
    Stub(2, 16)                                             // 0x46 BIT 0,(HL)
    Stub(2, 8)                                              // 0x47 BIT 0,A
    Stub(2, 8)                                              // 0x48 BIT 1,B
    Stub(2, 8)                                              // 0x49 BIT 1,C
    Stub(2, 8)                                              // 0x4A BIT 1,D
    Stub(2, 8)                                              // 0x4B BIT 1,E
    Stub(2, 8)                                              // 0x4C BIT 1,H
    Stub(2, 8)                                              // 0x4D BIT 1,L
    Stub(2, 16)                                             // 0x4E BIT 1,(HL)
    Stub(2, 8)                                              // 0x4F BIT 1,A
    Stub(2, 8)                                              // 0x50 BIT 2,B
    Stub(2, 8)                                              // 0x51 BIT 2,C
    Stub(2, 8)                                              // 0x52 BIT 2,D
    Stub(2, 8)                                              // 0x53 BIT 2,E
    Stub(2, 8)                                              // 0x54 BIT 2,H
    Stub(2, 8)                                              // 0x55 BIT 2,L
    Stub(2, 16)                                             // 0x56 BIT 2,(HL)
    Stub(2, 8)                                              // 0x57 BIT 2,A
    Stub(2, 8)                                              // 0x58 BIT 3,B
    Stub(2, 8)                                              // 0x59 BIT 3,C
    Stub(2, 8)                                              // 0x5A BIT 3,D
    Stub(2, 8)                                              // 0x5B BIT 3,E
    Stub(2, 8)                                              // 0x5C BIT 3,H
    Stub(2, 8)                                              // 0x5D BIT 3,L
    Stub(2, 16)                                             // 0x5E BIT 3,(HL)
    Stub(2, 8)                                              // 0x5F BIT 3,A
    Stub(2, 8)                                              // 0x60 BIT 4,B
    Stub(2, 8)                                              // 0x61 BIT 4,C
    Stub(2, 8)                                              // 0x62 BIT 4,D
    Stub(2, 8)                                              // 0x63 BIT 4,E
    Stub(2, 8)                                              // 0x64 BIT 4,H
    Stub(2, 8)                                              // 0x65 BIT 4,L
    Stub(2, 16)                                             // 0x66 BIT 4,(HL)
    Stub(2, 8)                                              // 0x67 BIT 4,A
    Stub(2, 8)                                              // 0x68 BIT 5,B
    Stub(2, 8)                                              // 0x69 BIT 5,C
    Stub(2, 8)                                              // 0x6A BIT 5,D
    Stub(2, 8)                                              // 0x6B BIT 5,E
    Stub(2, 8)                                              // 0x6C BIT 5,H
    Stub(2, 8)                                              // 0x6D BIT 5,L
    Stub(2, 16)                                             // 0x6E BIT 5,(HL)
    Stub(2, 8)                                              // 0x6F BIT 5,A
    Stub(2, 8)                                              // 0x70 BIT 6,B
    Stub(2, 8)                                              // 0x71 BIT 6,C
    Stub(2, 8)                                              // 0x72 BIT 6,D
    Stub(2, 8)                                              // 0x73 BIT 6,E
    Stub(2, 8)                                              // 0x74 BIT 6,H
    Stub(2, 8)                                              // 0x75 BIT 6,L
    Stub(2, 16)                                             // 0x76 BIT 6,(HL)
    Stub(2, 8)                                              // 0x77 BIT 6,A
    Stub(2, 8)                                              // 0x78 BIT 7,B
    Stub(2, 8)                                              // 0x79 BIT 7,C
    Stub(2, 8)                                              // 0x7A BIT 7,D
    Stub(2, 8)                                              // 0x7B BIT 7,E
    Bit(7, Reg8(H), 2, 8)                                   // 0x7C BIT 7,H
    Stub(2, 8)                                              // 0x7D BIT 7,L
    Stub(2, 16)                                             // 0x7E BIT 7,(HL)
    Stub(2, 8)                                              // 0x7F BIT 7,A
    Stub(2, 8)                                              // 0x80 RES 0,B
    Stub(2, 8)                                              // 0x81 RES 0,C
    Stub(2, 8)                                              // 0x82 RES 0,D
    Stub(2, 8)                                              // 0x83 RES 0,E
    Stub(2, 8)                                              // 0x84 RES 0,H
    Stub(2, 8)                                              // 0x85 RES 0,L
    Stub(2, 16)                                             // 0x86 RES 0,(HL)
    Stub(2, 8)                                              // 0x87 RES 0,A
    Stub(2, 8)                                              // 0x88 RES 1,B
    Stub(2, 8)                                              // 0x89 RES 1,C
    Stub(2, 8)                                              // 0x8A RES 1,D
    Stub(2, 8)                                              // 0x8B RES 1,E
    Stub(2, 8)                                              // 0x8C RES 1,H
    Stub(2, 8)                                              // 0x8D RES 1,L
    Stub(2, 16)                                             // 0x8E RES 1,(HL)
    Stub(2, 8)                                              // 0x8F RES 1,A
    Stub(2, 8)                                              // 0x90 RES 2,B
    Stub(2, 8)                                              // 0x91 RES 2,C
    Stub(2, 8)                                              // 0x92 RES 2,D
    Stub(2, 8)                                              // 0x93 RES 2,E
    Stub(2, 8)                                              // 0x94 RES 2,H
    Stub(2, 8)                                              // 0x95 RES 2,L
    Stub(2, 16)                                             // 0x96 RES 2,(HL)
    Stub(2, 8)                                              // 0x97 RES 2,A
    Stub(2, 8)                                              // 0x98 RES 3,B
    Stub(2, 8)                                              // 0x99 RES 3,C
    Stub(2, 8)                                              // 0x9A RES 3,D
    Stub(2, 8)                                              // 0x9B RES 3,E
    Stub(2, 8)                                              // 0x9C RES 3,H
    Stub(2, 8)                                              // 0x9D RES 3,L
    Stub(2, 16)                                             // 0x9E RES 3,(HL)
    Stub(2, 8)                                              // 0x9F RES 3,A
    Stub(2, 8)                                              // 0xA0 RES 4,B
    Stub(2, 8)                                              // 0xA1 RES 4,C
    Stub(2, 8)                                              // 0xA2 RES 4,D
    Stub(2, 8)                                              // 0xA3 RES 4,E
    Stub(2, 8)                                              // 0xA4 RES 4,H
    Stub(2, 8)                                              // 0xA5 RES 4,L
    Stub(2, 16)                                             // 0xA6 RES 4,(HL)
    Stub(2, 8)                                              // 0xA7 RES 4,A
    Stub(2, 8)                                              // 0xA8 RES 5,B
    Stub(2, 8)                                              // 0xA9 RES 5,C
    Stub(2, 8)                                              // 0xAA RES 5,D
    Stub(2, 8)                                              // 0xAB RES 5,E
    Stub(2, 8)                                              // 0xAC RES 5,H
    Stub(2, 8)                                              // 0xAD RES 5,L
    Stub(2, 16)                                             // 0xAE RES 5,(HL)
    Stub(2, 8)                                              // 0xAF RES 5,A
    Stub(2, 8)                                              // 0xB0 RES 6,B
    Stub(2, 8)                                              // 0xB1 RES 6,C
    Stub(2, 8)                                              // 0xB2 RES 6,D
    Stub(2, 8)                                              // 0xB3 RES 6,E
    Stub(2, 8)                                              // 0xB4 RES 6,H
    Stub(2, 8)                                              // 0xB5 RES 6,L
    Stub(2, 16)                                             // 0xB6 RES 6,(HL)
    Stub(2, 8)                                              // 0xB7 RES 6,A
    Stub(2, 8)                                              // 0xB8 RES 7,B
    Stub(2, 8)                                              // 0xB9 RES 7,C
    Stub(2, 8)                                              // 0xBA RES 7,D
    Stub(2, 8)                                              // 0xBB RES 7,E
    Stub(2, 8)                                              // 0xBC RES 7,H
    Stub(2, 8)                                              // 0xBD RES 7,L
    Stub(2, 16)                                             // 0xBE RES 7,(HL)
    Stub(2, 8)                                              // 0xBF RES 7,A
    Stub(2, 8)                                              // 0xC0 SET 0,B
    Stub(2, 8)                                              // 0xC1 SET 0,C
    Stub(2, 8)                                              // 0xC2 SET 0,D
    Stub(2, 8)                                              // 0xC3 SET 0,E
    Stub(2, 8)                                              // 0xC4 SET 0,H
    Stub(2, 8)                                              // 0xC5 SET 0,L
    Stub(2, 16)                                             // 0xC6 SET 0,(HL)
    Stub(2, 8)                                              // 0xC7 SET 0,A
    Stub(2, 8)                                              // 0xC8 SET 1,B
    Stub(2, 8)                                              // 0xC9 SET 1,C
    Stub(2, 8)                                              // 0xCA SET 1,D
    Stub(2, 8)                                              // 0xCB SET 1,E
    Stub(2, 8)                                              // 0xCC SET 1,H
    Stub(2, 8)                                              // 0xCD SET 1,L
    Stub(2, 16)                                             // 0xCE SET 1,(HL)
    Stub(2, 8)                                              // 0xCF SET 1,A
    Stub(2, 8)                                              // 0xD0 SET 2,B
    Stub(2, 8)                                              // 0xD1 SET 2,C
    Stub(2, 8)                                              // 0xD2 SET 2,D
    Stub(2, 8)                                              // 0xD3 SET 2,E
    Stub(2, 8)                                              // 0xD4 SET 2,H
    Stub(2, 8)                                              // 0xD5 SET 2,L
    Stub(2, 16)                                             // 0xD6 SET 2,(HL)
    Stub(2, 8)                                              // 0xD7 SET 2,A
    Stub(2, 8)                                              // 0xD8 SET 3,B
    Stub(2, 8)                                              // 0xD9 SET 3,C
    Stub(2, 8)                                              // 0xDA SET 3,D
    Stub(2, 8)                                              // 0xDB SET 3,E
    Stub(2, 8)                                              // 0xDC SET 3,H
    Stub(2, 8)                                              // 0xDD SET 3,L
    Stub(2, 16)                                             // 0xDE SET 3,(HL)
    Stub(2, 8)                                              // 0xDF SET 3,A
    Stub(2, 8)                                              // 0xE0 SET 4,B
    Stub(2, 8)                                              // 0xE1 SET 4,C
    Stub(2, 8)                                              // 0xE2 SET 4,D
    Stub(2, 8)                                              // 0xE3 SET 4,E
    Stub(2, 8)                                              // 0xE4 SET 4,H
    Stub(2, 8)                                              // 0xE5 SET 4,L
    Stub(2, 16)                                             // 0xE6 SET 4,(HL)
    Stub(2, 8)                                              // 0xE7 SET 4,A
    Stub(2, 8)                                              // 0xE8 SET 5,B
    Stub(2, 8)                                              // 0xE9 SET 5,C
    Stub(2, 8)                                              // 0xEA SET 5,D
    Stub(2, 8)                                              // 0xEB SET 5,E
    Stub(2, 8)                                              // 0xEC SET 5,H
    Stub(2, 8)                                              // 0xED SET 5,L
    Stub(2, 16)                                             // 0xEE SET 5,(HL)
    Stub(2, 8)                                              // 0xEF SET 5,A
    Stub(2, 8)                                              // 0xF0 SET 6,B
    Stub(2, 8)                                              // 0xF1 SET 6,C
    Stub(2, 8)                                              // 0xF2 SET 6,D
    Stub(2, 8)                                              // 0xF3 SET 6,E
    Stub(2, 8)                                              // 0xF4 SET 6,H
    Stub(2, 8)                                              // 0xF5 SET 6,L
    Stub(2, 16)                                             // 0xF6 SET 6,(HL)
    Stub(2, 8)                                              // 0xF7 SET 6,A
    Stub(2, 8)                                              // 0xF8 SET 7,B
    Stub(2, 8)                                              // 0xF9 SET 7,C
    Stub(2, 8)                                              // 0xFA SET 7,D
    Stub(2, 8)                                              // 0xFB SET 7,E
    Stub(2, 8)                                              // 0xFC SET 7,H
    Stub(2, 8)                                              // 0xFD SET 7,L
    Stub(2, 16)                                             // 0xFE SET 7,(HL)
    Stub(2, 8)                                              // 0xFF SET 7,A
};
