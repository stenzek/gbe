#include "jit_x86.h"
#include "jit_table.h"
#include "xbyak.h"

#define CPU_STATE_REGISTER esi

struct InstructionBuffer
{
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
        mov(esp, ebp);
        pop(edi);
        pop(CPU_STATE_REGISTER);
        pop(ebp);
        ret();
    }

    void interpreter_fallback()
    {
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

    void begin_instruction(const JitTable::Instruction *instruction)
    {
        uint32 pc_class_offset = offsetof(CPU, m_registers.PC);

        cycles(instruction->length * 4);
        //add(ptr[CPU_STATE_REGISTER + pc_class_offset], instruction->length);
        mov(ax, ptr[CPU_STATE_REGISTER + pc_class_offset]);
        add(ax, instruction->length);
        mov(ptr[CPU_STATE_REGISTER + pc_class_offset], ax);
    }

    void load_reg(const Xbyak::Operand &dst, CPU::Reg8 reg)
    {
        uint32 class_offset = offsetof(CPU, m_registers.reg8[0]) + reg * sizeof(uint8);
        mov(dst, ptr[CPU_STATE_REGISTER + class_offset]);
    }

    void store_reg(CPU::Reg8 reg, const Xbyak::Operand &src)
    {
        uint32 class_offset = offsetof(CPU, m_registers.reg8[0]) + reg * sizeof(uint8);
        mov(ptr[CPU_STATE_REGISTER + class_offset], src);
    }

    void load_regpair(const Xbyak::Operand &dst, CPU::Reg16 reg)
    {
        uint32 class_offset = offsetof(CPU, m_registers.reg16[0]) + reg * sizeof(uint16);
        mov(dst, ptr [CPU_STATE_REGISTER + class_offset]);
    }

    void store_regpair(CPU::Reg16 reg, const Xbyak::Operand &src)
    {
        uint32 class_offset = offsetof(CPU, m_registers.reg16[0]) + reg * sizeof(uint16);
        mov(ptr[CPU_STATE_REGISTER + class_offset], src);
    }

    void memory_read_addr_from_reg(const Xbyak::Operand &dst, CPU::Reg16 address_register)
    {

    }

    void memory_read_addr(const Xbyak::Operand &dst, uint16 address)
    {

    }

    void memory_write_addr_from_reg(CPU::Reg16 address_register, const Xbyak::Operand &src)
    {

    }

    void memory_write_addr(uint16 address, const Xbyak::Operand &src)
    {

    }

    void memory_read_word(const Xbyak::Operand &dst, uint16 address)
    {

    }

    void memory_write_word(uint16 address, const Xbyak::Operand &src)
    {

    }

    void compile_inc16(const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(instruction);
        load_regpair(ax, instruction->operand.reg16);
        inc(ax);
        store_regpair(instruction->operand.reg16, ax);
        delay_cycle();
    }

    void compile_dec16(const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(instruction);
        load_regpair(ax, instruction->operand.reg16);
        dec(ax);
        store_regpair(instruction->operand.reg16, ax);
        delay_cycle();
    }

    void compile_load(const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(instruction);

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
                    mov(ah, buffer->operand8);
                    break;

                    // 8-bit load from memory location in register
                case JitTable::Instruction::AddressMode_Mem16:
                    {
                        memory_read_addr_from_reg(ah, source->reg16);

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
                    memory_read_addr(ah, buffer->operand16);
                    break;

                default:
                    UnreachableCode();
                    break;
                }

                // store to register state
                store_reg(destination->reg8, ah);
            }
            break;

            // 16-bit load
        case JitTable::Instruction::AddressMode_Reg16:
            {
                // only can load bytes not words
                DebugAssert(source->mode == JitTable::Instruction::AddressMode_Imm16);
                memory_read_addr(ax, buffer->operand16);
                store_regpair(destination->reg16, ax);
            }
            break;

        default:
            UnreachableCode();
            break;
        }
    }

    void compile_store(const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(instruction);

        const JitTable::Instruction::Operand *destination = &instruction->operand;
        const JitTable::Instruction::Operand *source = &instruction->operand2;
        switch (source->mode)
        {
            // 8-bit store
        case JitTable::Instruction::AddressMode_Reg8:
        case JitTable::Instruction::AddressMode_Imm8:
            {
                // register -> memory, immediate -> memory
                if (source->mode == JitTable::Instruction::AddressMode_Reg8)
                    load_reg(ah, source->reg8);
                else
                    mov(ah, buffer->operand8);

                // write to memory
                switch (destination->mode)
                {
                case JitTable::Instruction::AddressMode_Mem16:
                    {
                        memory_write_addr_from_reg(destination->reg16, ah);

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
                    memory_write_addr(buffer->operand16, ah);
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

    void compile_move(const JitTable::Instruction *instruction, const InstructionBuffer *buffer)
    {
        begin_instruction(instruction);

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

    uint32 current_virtual_address = block->StartVirtualAddress;
    uint32 last_virtual_address = current_virtual_address;
    for (uint32 instruction_count = 0; instruction_count < block->InstructionCount; instruction_count++)
    {
        InstructionBuffer buffer;
        Y_memzero(&buffer, sizeof(buffer));

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
                buffer.operand8 = ReadVirtualAddress(current_virtual_address + 2);
            else if (instruction->length == 3)
                buffer.operand16 = (uint16)ReadVirtualAddress(current_virtual_address + 2) | ((uint16)ReadVirtualAddress(current_virtual_address + 3) << 8);
        }        

        // main code generator
        switch (instruction->type)
        {
//         case JitTable::Instruction::Type_Load:      emitter->compile_load(instruction, &buffer);        break;
//         case JitTable::Instruction::Type_Store:     emitter->compile_store(instruction, &buffer);       break;
//         case JitTable::Instruction::Type_Move:      emitter->compile_move(instruction, &buffer);        break;
//         case JitTable::Instruction::Type_INC16:     emitter->compile_inc16(instruction, &buffer);       break;
//         case JitTable::Instruction::Type_DEC16:     emitter->compile_dec16(instruction, &buffer);       break;
        default:                                    emitter->interpreter_fallback();                    break;
        }

        // update address
        last_virtual_address = current_virtual_address;
        current_virtual_address += instruction->length;
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

JitBase *JitBase::CreateJitCPU(System *system)
{
    return new JitX86(system);
}
