#include "jit_x86.h"
#include "jit_table.h"
#include "xbyak.h"

#define CPU_STATE_REGISTER esi

struct InstructionBuffer
{
    uint16 real_address;
    uint32 virtual_address;
    uint8 opcode;
    union
    {
        uint16 operand16;
        uint8 operand8;
        int8 operands8;
    };
};

class JitX86Emitter : public Xbyak::CodeGenerator
{
public:
    JitX86Emitter(size_t maxSize = Xbyak::DEFAULT_MAX_CODE_SIZE, void *userPtr = 0, Xbyak::Allocator *allocator = 0)
        : Xbyak::CodeGenerator(maxSize, userPtr, allocator)
    {

    }

    ~JitX86Emitter()
    {

    }

    void start_block()
    {
        // ABI notes: trash EAX, ECX, EDX
        // ESI = cpu struct
        push(ebp);
        push(CPU_STATE_REGISTER);
        push(edi);
        mov(ebp, esp);
        mov(CPU_STATE_REGISTER, ptr[ebp + 16]);
    }

    void end_block()
    {
        L(".block_exit");
        mov(esp, ebp);
        pop(edi);
        pop(CPU_STATE_REGISTER);
        pop(ebp);
        ret();
    }

    void jump_to_end()
    {
        jmp(".block_exit", CodeGenerator::T_NEAR);
    }

    void emit_jump_label(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        uint16 addr = buffer->real_address;
        for (const auto &jump : block->Jumps)
        {
            if (jump.JumpTarget == addr)
            {
                SmallString jump_label;
                jump_label.Format(".JT_0x%04X", addr);
                L(jump_label.GetCharArray());
                break;
            }
        }
    }

    void interpreter_fallback(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        emit_jump_label(block, instruction, buffer);

        push(CPU_STATE_REGISTER);
        call(&JitX86::InterpreterFallbackTrampoline);
        add(esp, 4);
    }

    void delay_cycle()
    {
        cycles(4);
    }

    void cycles(uint32 count)
    {
        push(count);
        push(CPU_STATE_REGISTER);
        call(&JitX86::AddCyclesTrampoline);
        add(esp, 8);
    }

    void interrupt_test(uint16 address)
    {
        SmallString ime_disabled_label;
        SmallString no_interrupt_label;
        ime_disabled_label.Format(".ime_disabled_0x%04X", address);
        no_interrupt_label.Format(".no_interrupt_0x%04X", address);

        // test for interrupts
        // todo: optimize
        mov(al, byte[CPU_STATE_REGISTER + offsetof(CPU, m_registers.IME)]);
        test(al, al);
        jz(ime_disabled_label.GetCharArray());
        mov(al, byte[CPU_STATE_REGISTER + offsetof(CPU, m_registers.IE)]);
        mov(ah, byte[CPU_STATE_REGISTER + offsetof(CPU, m_registers.IF)]);
        test(al, ah);
        jz(no_interrupt_label.GetCharArray());
        push(CPU_STATE_REGISTER);
        call(&JitX86::InterruptFireTrampoline);
        add(esp, 4);
        jump_to_end();
        L(no_interrupt_label.GetCharArray());
    }

    void begin_instruction(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        const uint32 pc_class_offset = offsetof(CPU, m_registers.PC);       

        emit_jump_label(block, instruction, buffer);
        interrupt_test(buffer->real_address);
        cycles(instruction->length * 4);
        //add(ptr[CPU_STATE_REGISTER + pc_class_offset], instruction->length);
        mov(ax, ptr[CPU_STATE_REGISTER + pc_class_offset]);
        add(ax, instruction->length);
        mov(ptr[CPU_STATE_REGISTER + pc_class_offset], ax);
    }

    void load_reg(const Xbyak::Reg &dst, CPU::Reg8 reg)
    {
        uint32 class_offset = offsetof(CPU, m_registers.reg8[0]) + reg * sizeof(uint8);
        if (dst.isBit(32))
            movzx(dst, byte[CPU_STATE_REGISTER + class_offset]);
        else
            mov(dst, ptr[CPU_STATE_REGISTER + class_offset]);
    }

    void store_reg(CPU::Reg8 reg, const Xbyak::Operand &src)
    {
        DebugAssert(src.isBit(8));
        uint32 class_offset = offsetof(CPU, m_registers.reg8[0]) + reg * sizeof(uint8);
        mov(byte[CPU_STATE_REGISTER + class_offset], src);
    }

    void load_regpair(const Xbyak::Reg &dst, CPU::Reg16 reg)
    {
        uint32 class_offset = offsetof(CPU, m_registers.reg16[0]) + reg * sizeof(uint16);
        if (dst.isBit(32))
            movzx(dst, word[CPU_STATE_REGISTER + class_offset]);
        else
            mov(dst, word[CPU_STATE_REGISTER + class_offset]);
    }

    void store_regpair(CPU::Reg16 reg, const Xbyak::Operand &src)
    {
        DebugAssert(src.isBit(16));
        uint32 class_offset = offsetof(CPU, m_registers.reg16[0]) + reg * sizeof(uint16);
        mov(word[CPU_STATE_REGISTER + class_offset], src);
    }

    // memory -> al
    void memory_read_addr_from_reg(CPU::Reg16 address_register)
    {
        load_regpair(eax, address_register);
        push(eax);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryReadTrampoline);
        add(esp, 8);
    }

    // memory -> al
    void memory_read_addr(uint16 address)
    {
        push(address);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryReadTrampoline);
        add(esp, 8);
    }

    // reg -> memory
    void memory_write_addr_from_reg(CPU::Reg16 address_register, const Xbyak::Operand &src)
    {
        // push only takes ax, eax, rax, abi says parameters must be word size
        DebugAssert(src.isBit(32));
        push(src);
        load_regpair(eax, address_register);
        push(eax);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryWriteTrampoline);
        add(esp, 12);
    }

    // reg -> memory
    void memory_write_addr(uint16 address, const Xbyak::Operand &src)
    {
        // push only takes ax, eax, rax, abi says parameters must be word size
        DebugAssert(src.isBit(32));
        push(src);
        push(address);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryWriteTrampoline);
        add(esp, 12);
    }

    // memory -> ax
    void memory_read_word(uint16 address)
    {
        push(address);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryReadWordTrampoline);
        add(esp, 8);
    }

    // reg -> memory
    void memory_write_word(uint16 address, const Xbyak::Operand &src)
    {
        DebugAssert(src.isBit(32));
        push(src);
        push(address);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryReadWordTrampoline);
        add(esp, 12);
    }

    // push byte, blows away eax
    void push_byte(const Xbyak::Operand &src)
    {
        if (!src.isBit(32))
        {
            movzx(eax, src);
            push(eax);
        }
        else
        {
            push(src);
        }

        // careful here to sign-extend but only increment lower 16 bits
        load_regpair(eax, CPU::Reg16_SP);
        dec(ax);
        store_regpair(CPU::Reg16_SP, ax);
        push(eax);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryWriteTrampoline);
        add(esp, 12);
    }

    // push word, blows away eax
    void push_word(const Xbyak::Operand &src)
    {
        if (!src.isBit(32))
        {
            movzx(eax, src);
            push(eax);
        }
        else
        {
            push(src);
        }

        // careful here to sign-extend but only increment lower 16 bits
        load_regpair(eax, CPU::Reg16_SP);
        sub(ax, 2);
        store_regpair(CPU::Reg16_SP, ax);
        push(eax);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryWriteWordTrampoline);
        add(esp, 12);
    }

    // pop byte -> al
    void pop_byte()
    {
        load_regpair(eax, CPU::Reg16_SP);
        push(eax);
        inc(ax);
        store_regpair(CPU::Reg16_SP, ax);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryReadTrampoline);
        add(esp, 8);
    }

    // pop word -> ax
    void pop_word()
    {
        load_regpair(eax, CPU::Reg16_SP);
        push(eax);
        add(ax, 2);
        store_regpair(CPU::Reg16_SP, ax);
        push(CPU_STATE_REGISTER);
        call(&JitX86::MemoryReadWordTrampoline);
        add(esp, 8);
    }

    void compile_nop(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
    }

    void compile_load(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        const JitTable::Instruction::Operand *destination = &instruction->operand;
        const JitTable::Instruction::Operand *source = &instruction->operand2;
        switch (destination->mode)
        {
            // 8-bit load
        case JitTable::Instruction::AddressMode_Reg8:
            {
                switch (source->mode)
                {
                    // 8-bit load immediate
                case JitTable::Instruction::AddressMode_Imm8:
                    mov(al, buffer->operand8);
                    store_reg(destination->reg8, al);
                    break;

                    // 8-bit load from memory location in register
                case JitTable::Instruction::AddressMode_Mem16:
                    {
                        memory_read_addr_from_reg(source->reg16);
                        store_reg(destination->reg8, al);

                        // ldi/ldd TODO optimize
                        if (instruction->load_action == JitTable::Instruction::LoadStoreAction_IncrementAddress)
                        {
                            load_regpair(ax, source->reg16);
                            inc(ax);
                            store_regpair(source->reg16, ax);
                        }
                        else if (instruction->load_action == JitTable::Instruction::LoadStoreAction_DecrementAddress)
                        {
                            load_regpair(ax, source->reg16);
                            dec(ax);
                            store_regpair(source->reg16, ax);
                        }
                    }
                    break;

                    // 8-bit load from absolute memory location
                case JitTable::Instruction::AddressMode_Addr16:
                    memory_read_addr(buffer->operand16);
                    store_reg(destination->reg8, al);
                    break;

                default:
                    UnreachableCode();
                    break;
                }
            }
            break;

            // 16-bit load
        case JitTable::Instruction::AddressMode_Reg16:
            {
                // only can load bytes not words
                DebugAssert(source->mode == JitTable::Instruction::AddressMode_Imm16);
                mov(ax, buffer->operand16);
                store_regpair(destination->reg16, ax);
            }
            break;

        default:
            UnreachableCode();
            break;
        }
    }

    void compile_store(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        const JitTable::Instruction::Operand *destination = &instruction->operand;
        const JitTable::Instruction::Operand *source = &instruction->operand2;
        switch (source->mode)
        {
            // 8-bit store
        case JitTable::Instruction::AddressMode_Reg8:
        case JitTable::Instruction::AddressMode_Imm8:
            {
                // register -> memory, immediate -> memory
                // load and sign extend because it has to be written to memory
                if (source->mode == JitTable::Instruction::AddressMode_Reg8)
                    load_reg(eax, source->reg8);
                else
                    mov(eax, buffer->operand8);

                // write to memory
                switch (destination->mode)
                {
                case JitTable::Instruction::AddressMode_Mem16:
                    {
                        memory_write_addr_from_reg(destination->reg16, eax);

                        // ldi/ldd TODO optimize
                        if (instruction->load_action == JitTable::Instruction::LoadStoreAction_IncrementAddress)
                        {
                            load_regpair(ax, destination->reg16);
                            inc(ax);
                            store_regpair(destination->reg16, ax);
                        }
                        else if (instruction->load_action == JitTable::Instruction::LoadStoreAction_DecrementAddress)
                        {
                            load_regpair(ax, destination->reg16);
                            dec(ax);
                            store_regpair(destination->reg16, ax);
                        }

                        break;
                    }

                case JitTable::Instruction::AddressMode_Addr16:
                    memory_write_addr(buffer->operand16, eax);
                    break;

                default:
                    UnreachableCode();
                    break;
                }

            }
            break;

            // 16-bit store
        case JitTable::Instruction::AddressMode_Reg16:
            {
                // only to memory
                DebugAssert(destination->mode == JitTable::Instruction::AddressMode_Addr16);
                load_regpair(ax, source->reg16);
                memory_write_word(buffer->operand16, ax);
            }
            break;

        default:
            UnreachableCode();
            break;
        }
    }

    void compile_move(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        const JitTable::Instruction::Operand *destination = &instruction->operand;
        const JitTable::Instruction::Operand *source = &instruction->operand2;
        DebugAssert(source->mode == destination->mode);
        switch (destination->mode)
        {
        case JitTable::Instruction::AddressMode_Reg8:
            load_reg(ah, source->reg8);
            store_reg(destination->reg8, ah);
            break;

        case JitTable::Instruction::AddressMode_Reg16:
            load_regpair(ah, source->reg16);
            store_regpair(destination->reg16, ah);
            break;

        default:
            UnreachableCode();
            break;
        }
    }

    // blows away eax
    void predicate_test(JitTable::Instruction::Predicate predicate, String &skip_label)
    {
        // emit nothing for always
        if (predicate == JitTable::Instruction::Predicate_Always)
            return;

        // load flags register
        load_reg(ax, CPU::Reg8_F);

        // skip when predicate is not true
        switch (predicate)
        {
        case JitTable::Instruction::Predicate_Zero:
        case JitTable::Instruction::Predicate_NotZero:
            test(eax, CPU::FLAG_Z);
            (predicate == JitTable::Instruction::Predicate_Zero) ? jz(skip_label.GetCharArray(), CodeGenerator::T_SHORT) : jnz(skip_label.GetCharArray(), CodeGenerator::T_SHORT);
            break;

        case JitTable::Instruction::Predicate_Carry:
        case JitTable::Instruction::Predicate_NotCarry:
            test(eax, CPU::FLAG_C);
            (predicate == JitTable::Instruction::Predicate_Carry) ? jz(skip_label.GetCharArray(), CodeGenerator::T_SHORT) : jnz(skip_label.GetCharArray(), CodeGenerator::T_SHORT);
            break;

        default:
            UnreachableCode();
            break;
        }
    }

    void compile_jp(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        // test the predicate
        SmallString skip_label;
        skip_label.Format(".SKIP_0x%04X", buffer->real_address);
        predicate_test(instruction->predicate, skip_label);

        // code executed when jump is taken
        mov(ax, buffer->operand16);
        store_regpair(CPU::Reg16_PC, ax);
        delay_cycle();
        jump_to_end();

        // create skip label after it
        L(skip_label.GetCharArray());
    }

    void compile_jr(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        // work out where in the compiled code to jump to, if it lies outside the block, exit the block early
        SmallString target_label;
        uint16 effective_address = (buffer->operands8 < 0) ? (buffer->real_address + instruction->length - uint8(-buffer->operands8)) : (buffer->real_address + instruction->length + uint8(buffer->operands8));
        if (effective_address < block->StartRealAddress || effective_address > block->EndRealAddress)
        {
            // exit block
            target_label.Assign(".block_exit");
        }
        else
        {
            // jump to target
            target_label.Format(".JT_0x%04X", effective_address);
        }

        // test the predicate
        SmallString skip_label;
        skip_label.Format(".SKIP_0x%04X", buffer->real_address);
        predicate_test(instruction->predicate, skip_label);

        // code executed when jump is taken
        mov(ax, effective_address);
        store_regpair(CPU::Reg16_PC, ax);
        delay_cycle();
        jmp(target_label.GetCharArray(), CodeGenerator::T_NEAR);

        // create skip label after it
        L(skip_label.GetCharArray());
    }

    void compile_call(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        // test the predicate
        SmallString skip_label;
        skip_label.Format(".SKIP_0x%04X", buffer->real_address);
        predicate_test(instruction->predicate, skip_label);

        // code executed when jump is taken
        load_regpair(ax, CPU::Reg16_PC);
        push_word(ax);
        mov(ax, buffer->operand16);
        store_regpair(CPU::Reg16_PC, ax);
        delay_cycle();
        jump_to_end();

        // create skip label after it
        L(skip_label.GetCharArray());
    }

    void compile_ret(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        // test the predicate
        SmallString skip_label;
        skip_label.Format(".SKIP_0x%04X", buffer->real_address);
        predicate_test(instruction->predicate, skip_label);

        // code executed when jump is taken
        pop_word();
        store_regpair(CPU::Reg16_PC, ax);
        delay_cycle();
        jump_to_end();

        // create skip label after it
        L(skip_label.GetCharArray());
    }

    void compile_push(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
        load_regpair(ax, instruction->operand2.reg16);
        push_word(ax);
    }

    void compile_pop(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
        pop_word();

        // drop lower 4 bits of F
        if (instruction->operand.reg16 == CPU::Reg16_AF)
            and(eax, 0xFFF0);

        store_regpair(instruction->operand.reg16, ax);
    }

    void compile_inc16(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
        load_regpair(ax, instruction->operand.reg16);
        inc(ax);
        store_regpair(instruction->operand.reg16, ax);
        delay_cycle();
    }

    void compile_dec16(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
        load_regpair(ax, instruction->operand.reg16);
        dec(ax);
        store_regpair(instruction->operand.reg16, ax);
        delay_cycle();
    }

    // update flags based on value in al, for carry/zero, must be executed directly after the ALU op
    void update_flags(uint8 modify_mask, uint8 set_mask)
    {
        // http://reverseengineering.stackexchange.com/questions/9348/why-are-pushf-and-popf-so-slow
        // possibly use lahf/sahf instead of pushf?
        if (set_mask & (CPU::FLAG_Z | CPU::FLAG_C))
            pushf();

        // load old flags, clear out the ones we're modifying
        load_reg(ecx, CPU::Reg8_F);
        and(ecx, ~modify_mask);
        if (set_mask & CPU::FLAG_N)
            or(ecx, CPU::FLAG_N);

        // eax: value, ecx: Z80 flags, edx: temp
        if (set_mask & CPU::FLAG_Z)
        {
            mov(edx, dword[esp]);
            and(edx, (1 << 6)); // ZF
            shl(edx, 1);        // 6 -> 7 (FLAG_Z)
            or(ecx, edx);       // insert FLAG_Z
        }
        if (set_mask & CPU::FLAG_C)
        {
            mov(edx, dword[esp]);
            and(edx, (1 << 0)); // CF
            shl(edx, 4);        // 0 -> 4 (FLAG_C)
            or (ecx, edx);      // insert FLAG_C
        }
        if (set_mask & CPU::FLAG_H)
        {
            mov(dl, al);
            and(dl, 0xF);
            cmp(dl, 0xF);
            pushf();
            pop(edx);
            and(edx, (1 << 6)); // ZF
            shr(edx, 1);        // 6 -> 5 (FLAG_H)
            or(ecx, edx);       // insert FLAG_H
        }

        if (set_mask & (CPU::FLAG_Z | CPU::FLAG_C))
            add(esp, 4);

        // write flags, write value
        store_reg(CPU::Reg8_F, cl);
    }

    void compile_inc_dec(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        switch (instruction->operand.mode)
        {
        case JitTable::Instruction::AddressMode_Reg8:
            load_reg(al, instruction->operand.reg8);
            break;
        case JitTable::Instruction::AddressMode_Mem16:
            memory_read_addr_from_reg(instruction->operand.reg16);
            break;
        default:
            UnreachableCode();
            break;
        }

#if 0

        // load old flags
        load_reg(ecx, CPU::Reg8_F);

        // eax: value, ecx: Z80 flags, edx: temp
        inc(al);
        pushf();
        pop(edx);
        and(edx, (1 << 6)); // ZF
        shl(edx, 1);        // 6 -> 7 (FLAG_Z)
        and(ecx, 0x1F);     // clear old FLAG_Z, FLAG_N, FLAG_H
        or(ecx, edx);       // insert FLAG_Z
        mov(dl, al);
        and(dl, 0xF);
        cmp(dl, 0xF);
        pushf();
        pop(edx);
        and(edx, (1 << 6)); // ZF
        shr(edx, 1);        // 6 -> 5 (FLAG_H)
        or(ecx, edx);       // insert FLAG_H

        // write flags, write value
        store_reg(CPU::Reg8_F, cl);

#else

        if (instruction->type == JitTable::Instruction::Type_INC)
        {
            inc(al);
            update_flags(CPU::FLAG_Z | CPU::FLAG_N | CPU::FLAG_H, CPU::FLAG_Z | CPU::FLAG_H);
        }
        else
        {
            dec(al);
            update_flags(CPU::FLAG_Z | CPU::FLAG_N | CPU::FLAG_H, CPU::FLAG_Z | CPU::FLAG_N | CPU::FLAG_H);
        }

#endif

        switch (instruction->operand.mode)
        {
        case JitTable::Instruction::AddressMode_Reg8:
            store_reg(instruction->operand.reg8, al);
            break;
        case JitTable::Instruction::AddressMode_Mem16:
            memory_write_addr_from_reg(instruction->operand.reg16, eax);
            break;
        default:
            UnreachableCode();
            break;
        }
    }

    void compile_binops(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
       
        // load rhs
        switch (instruction->operand.mode)
        {
        case JitTable::Instruction::AddressMode_Imm8:
            mov(al, buffer->operand8);
            break;
        case JitTable::Instruction::AddressMode_Reg8:
            load_reg(al, instruction->operand.reg8);
            break;
        case JitTable::Instruction::AddressMode_Mem16:
            memory_read_addr_from_reg(instruction->operand.reg16);
            break;
        default:
            UnreachableCode();
            break;
        }

        // load lhs
        load_reg(cl, CPU::Reg8_A);

        // do op, this is reversed because update_flags trashes ecx/edx.
        switch (instruction->type)
        {
        case JitTable::Instruction::Type_AND:
            and(al, cl);
            update_flags(CPU::FLAG_Z | CPU::FLAG_N | CPU::FLAG_H | CPU::FLAG_C, CPU::FLAG_Z | CPU::FLAG_N);
            break;
        case JitTable::Instruction::Type_OR:
            or(al, cl);
            update_flags(CPU::FLAG_Z | CPU::FLAG_N | CPU::FLAG_H | CPU::FLAG_C, CPU::FLAG_Z);
            break;
        case JitTable::Instruction::Type_XOR:
            xor(al, cl);
            update_flags(CPU::FLAG_Z | CPU::FLAG_N | CPU::FLAG_H | CPU::FLAG_C, CPU::FLAG_Z);
            break;
        }

        // store result to A
        store_reg(CPU::Reg8_A, al);
    }


};

JitX86::JitX86(System *system)
    : JitBase(system)
{

}

JitX86::~JitX86()
{

}

JitBase::Block *JitX86::AllocateBlock(uint32 virtual_address, uint16 real_address)
{
    X86Block *block = new X86Block();
    block->StartVirtualAddress = virtual_address;
    block->StartRealAddress = real_address;
    block->InstructionCount = 0;
    block->ByteCount = 0;
    block->Emitter = nullptr;
    block->EntryPoint = nullptr;
    return block;
}

void JitX86::DestroyBlock(Block *block)
{
    X86Block *real_block = (X86Block *)block;
    delete real_block->Emitter;
    delete block;
}

bool JitX86::CompileBlock(Block *block)
{
    // fixme: allocators..
    JitX86Emitter *emitter = new JitX86Emitter(Xbyak::DEFAULT_MAX_CODE_SIZE * 2);
    emitter->start_block();

    uint16 current_real_address = block->StartRealAddress;
    uint32 current_virtual_address = block->StartVirtualAddress;
    uint32 last_virtual_address = current_virtual_address;
    for (uint32 instruction_count = 0; instruction_count < block->InstructionCount; instruction_count++)
    {
        InstructionBuffer buffer;
        Y_memzero(&buffer, sizeof(buffer));

        buffer.real_address = current_real_address;
        buffer.virtual_address = current_virtual_address;
        buffer.opcode = ReadVirtualAddress(current_virtual_address);
        const JitTable::Instruction *instruction = &JitTable::instructions[buffer.opcode];
        if (instruction->type == JitTable::Instruction::Type_Prefix)
        {
            buffer.opcode = ReadVirtualAddress(current_virtual_address + 1);
            instruction = &JitTable::cb_instructions[buffer.opcode];
            if (instruction->length == 3)
                buffer.operand8 = ReadVirtualAddress(current_virtual_address + 2);
            else if (instruction->length == 4)
                buffer.operand16 = (uint16)ReadVirtualAddress(current_virtual_address + 2) | ((uint16)ReadVirtualAddress(current_virtual_address + 3) << 8);
        }
        else
        {
            if (instruction->length == 2)
                buffer.operand8 = ReadVirtualAddress(current_virtual_address + 1);
            else if (instruction->length == 3)
                buffer.operand16 = (uint16)ReadVirtualAddress(current_virtual_address + 1) | ((uint16)ReadVirtualAddress(current_virtual_address + 2) << 8);
        }        

        // main code generator
        switch (instruction->type)
        {
        case JitTable::Instruction::Type_Nop:       emitter->compile_nop(block, instruction, &buffer);          break;
        case JitTable::Instruction::Type_Load:      emitter->compile_load(block, instruction, &buffer);         break;
        case JitTable::Instruction::Type_Store:     emitter->compile_store(block, instruction, &buffer);        break;
        case JitTable::Instruction::Type_Move:      emitter->compile_move(block, instruction, &buffer);         break;
        case JitTable::Instruction::Type_JP:        emitter->compile_jp(block, instruction, &buffer);           break;
        case JitTable::Instruction::Type_JR:        emitter->compile_jr(block, instruction, &buffer);           break;
        case JitTable::Instruction::Type_CALL:      emitter->compile_call(block, instruction, &buffer);         break;
        case JitTable::Instruction::Type_RET:       emitter->compile_ret(block, instruction, &buffer);          break;
        case JitTable::Instruction::Type_PUSH:      emitter->compile_push(block, instruction, &buffer);         break;
        case JitTable::Instruction::Type_POP:       emitter->compile_pop(block, instruction, &buffer);          break;
        case JitTable::Instruction::Type_INC:       emitter->compile_inc_dec(block, instruction, &buffer);      break;
        case JitTable::Instruction::Type_DEC:       emitter->compile_inc_dec(block, instruction, &buffer);      break;
        case JitTable::Instruction::Type_INC16:     emitter->compile_inc16(block, instruction, &buffer);        break;
        case JitTable::Instruction::Type_DEC16:     emitter->compile_dec16(block, instruction, &buffer);        break;
        case JitTable::Instruction::Type_AND:       emitter->compile_binops(block, instruction, &buffer);       break;
        //case JitTable::Instruction::Type_OR:        emitter->compile_binops(block, instruction, &buffer);       break;
        //case JitTable::Instruction::Type_XOR:       emitter->compile_binops(block, instruction, &buffer);       break;
        default:                                    emitter->interpreter_fallback(block, instruction, &buffer); break;
        }

        // update address
        last_virtual_address = current_virtual_address;
        current_virtual_address += instruction->length;
        current_real_address += instruction->length;
    }

    emitter->end_block();

    X86Block *real_block = (X86Block *)block;
    real_block->EntryPoint = emitter->getCode<void(__cdecl *)(void *)>();
    real_block->Emitter = emitter;
    return true;
}

void JitX86::ExecuteBlock(Block *block)
{
    X86Block *real_block = (X86Block *)block;
    real_block->EntryPoint(this);
}

void JitX86::AddCyclesTrampoline(JitX86 *this_ptr, uint32 cycles)
{
    this_ptr->DelayCycles(cycles);
}

void JitX86::InterpreterFallbackTrampoline(JitX86 *this_ptr)
{
    this_ptr->InterpreterExecuteInstruction();
}

void JitX86::InterruptFireTrampoline(JitX86 *this_ptr)
{
    this_ptr->InterruptTest();
}

uint8 JitX86::MemoryReadTrampoline(JitX86 *this_ptr, uint16 address)
{
    return this_ptr->MemReadByte(address);
}

void JitX86::MemoryWriteTrampoline(JitX86 *this_ptr, uint16 address, uint8 data)
{
    this_ptr->MemWriteByte(address, data);
}

uint16 JitX86::MemoryReadWordTrampoline(JitX86 *this_ptr, uint16 address)
{
    return this_ptr->MemReadWord(address);
}

void JitX86::MemoryWriteWordTrampoline(JitX86 *this_ptr, uint16 address, uint16 data)
{
    this_ptr->MemWriteWord(address, data);
}

JitBase *JitBase::CreateJitCPU(System *system)
{
    return new JitX86(system);
}
