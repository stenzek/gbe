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

    void compile_nop(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);
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

    void compile_jp(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        if (instruction->predicate != JitTable::Instruction::Predicate_Always)
        {
            interpreter_fallback(block, instruction, buffer);
            return;
        }

        begin_instruction(block, instruction, buffer);
        mov(ax, buffer->operand16);
        store_regpair(CPU::Reg16_PC, ax);
        delay_cycle();
        jump_to_end();
    }

    void compile_jr(JitX86::Block *block, const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(block, instruction, buffer);

        // work out where in the compiled code to jump to
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

        // always jumps are easy
        if (instruction->predicate == JitTable::Instruction::Predicate_Always)
        {
            mov(ax, effective_address);
            store_regpair(CPU::Reg16_PC, ax);
            jmp(target_label.GetCharArray(), CodeGenerator::T_NEAR);
            delay_cycle();
        }
        else
        {
            // setup skipped label
            SmallString skip_label;
            skip_label.Format(".SKIP_0x%04X", buffer->real_address);

            // load flags register
            load_reg(eax, CPU::Reg8_F);

            // test predicate, jump if failed
            switch (instruction->predicate)
            {
            case JitTable::Instruction::Predicate_Zero:
            case JitTable::Instruction::Predicate_NotZero:
                test(eax, CPU::FLAG_Z);
                (instruction->predicate == JitTable::Instruction::Predicate_Zero) ? jz(skip_label.GetCharArray(), CodeGenerator::T_SHORT) : jnz(skip_label.GetCharArray(), CodeGenerator::T_SHORT);
                break;

            case JitTable::Instruction::Predicate_Carry:
            case JitTable::Instruction::Predicate_NotCarry:
                test(eax, CPU::FLAG_C);
                (instruction->predicate == JitTable::Instruction::Predicate_Carry) ? jz(skip_label.GetCharArray(), CodeGenerator::T_SHORT) : jnz(skip_label.GetCharArray(), CodeGenerator::T_SHORT);
                break;

            default:
                UnreachableCode();
                break;
            }

            // code executed when jump is taken
            mov(ax, effective_address);
            store_regpair(CPU::Reg16_PC, ax);
            delay_cycle();
            jmp(target_label.GetCharArray(), CodeGenerator::T_NEAR);

            // create skip label after it
            L(skip_label.GetCharArray());
        }
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
    JitX86Emitter *emitter = new JitX86Emitter();
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
        case JitTable::Instruction::Type_INC16:     emitter->compile_inc16(block, instruction, &buffer);        break;
        case JitTable::Instruction::Type_DEC16:     emitter->compile_dec16(block, instruction, &buffer);        break;
        case JitTable::Instruction::Type_JP:        emitter->compile_jp(block, instruction, &buffer);           break;
        case JitTable::Instruction::Type_JR:        emitter->compile_jr(block, instruction, &buffer);           break;
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
