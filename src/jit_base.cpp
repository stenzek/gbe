#include "jit_base.h"
#include "jit_table.h"
#include "cartridge.h"
#include "system.h"
#include "YBaseLib/Log.h"
Log_SetChannel(JitBase);

JitBase::JitBase(System *system) 
    : CPU(system)
{

}

JitBase::~JitBase()
{

}

void JitBase::ExecuteInstruction()
{
    // cpu disabled for memory transfer?
    if (m_disabled)
    {
        DelayCycle();
        return;
    }

    // interrupts enabled?
    if (m_registers.IME)
        InterruptTest();

    // if halted, simulate a single cycle to keep the display/audio going
    if (m_halted)
    {
        DelayCycle();
        return;
    }

    // skip non-jitable code pointer
    if (!InJittableRange(m_registers.PC))
    {
        InterpreterExecuteInstruction();
        return;
    }

    // look at pc, and get virtual address
    uint16 real_address = m_registers.PC;
    uint32 virtual_address = GetVirtualAddress(real_address);

    // find a block
    Block *block;
    if (!LookupBlock(virtual_address, &block))
    {
        Log_DevPrintf("jit: block 0x%05X not found, compiling", virtual_address);

        // try to create this block
        block = CreateBlock(virtual_address, real_address);
//         if (block == nullptr)
//         {
//             // fallback to interpreter
//             Log_ErrorPrintf("jit: block 0x%05X failed compilation, falling back to interpreter", virtual_address);
//             InterpreterExecuteInstruction();
//             return;
//         }
    }

    // block was not compiled?
    //Log_DevPrintf("jit: executing block 0x%05X at %p", virtual_address, block->EntryPoint);
    if (block == nullptr)
        InterpreterExecuteInstruction();
    else
        ExecuteBlock(block);
}

bool JitBase::InJittableRange(uint16 real_address)
{
    // only jitting cart space atm
    if (real_address >= 0x8000)
        return false;

    // okay
    return true;
}

uint32 JitBase::GetVirtualAddress(uint16 address) const
{
    uint16 section = address & 0xF000;
    uint32 bank = 0;
    if (section >= 0x4000 && section < 0x8000)
    {
        Cartridge *cart = m_system->GetCartridge();
        if (cart != nullptr)
        {
            bank = cart->GetActiveROMBank1();
            address &= 0x3FFF;
        }
    }

    return (bank << 16) | (uint32)address;
}

uint8 JitBase::ReadVirtualAddress(uint32 address)
{
    Cartridge *cart = m_system->GetCartridge();
    DebugAssert(cart != nullptr);

    uint32 bank = address >> 16;
    uint16 offset = address & 0x3FFF;
    return cart->ReadRomBank(bank, offset);
}

bool JitBase::LookupBlock(uint32 virtual_address, Block **block_ptr)
{
    auto member = m_blocks.Find(virtual_address);
    if (member == nullptr)
        return false;

    *block_ptr = (member != nullptr) ? member->Value : nullptr;
    return true;
}

JitBase::Block *JitBase::CreateBlock(uint32 virtual_address, uint16 real_address)
{
    DebugAssert(InJittableRange(real_address));

    // analyse block
    Block *block = AllocateBlock(virtual_address, real_address);    
    if (AnalyseBlock(block))
    {
        // skip blocks with jumps for now
        if (block->Jumps.GetSize() > 0)
        {
            Log_WarningPrintf("jit: address 0x%05X has jumps, skipping", virtual_address);
            DestroyBlock(block);
            block = nullptr;
        }
        else
        {
            // compile block
            if (!CompileBlock(block))
            {
                Log_ErrorPrintf("jit: address 0x%05X failed compiling", virtual_address);
                DestroyBlock(block);
                block = nullptr;
            }
        }
    }
    else
    {
        Log_ErrorPrintf("jit: address 0x%05X failed analysis, not compiling", virtual_address);
        DestroyBlock(block);
        block = nullptr;
    }    

    // done!
    m_blocks.Insert(virtual_address, block);
    return block;
}

bool JitBase::AnalyseBlock(Block *block)
{
    Log_DevPrintf("analyse: start address 0x%05x (real 0x%04x)", block->StartVirtualAddress, block->StartRealAddress);

    block->InstructionCount = 0;
    block->ByteCount = 0;

    uint16 current_real_address = block->StartRealAddress;
    uint32 current_virtual_address = block->StartVirtualAddress;
    uint32 last_virtual_address = current_virtual_address;
    for (;;)
    {
        // must be in the same bank
        if ((current_virtual_address & 0xC000) != (last_virtual_address & 0xC000))
        {
            Log_ErrorPrintf("  block ends past bank boundary");
            return false;
        }

        // check address range
        if ((current_virtual_address & 0xFFFF) >= 0x4000)
        {
            Log_ErrorPrintf("  block ends past cart space");
            return false;
        }

        // read code byte
        uint8 code = ReadVirtualAddress(current_virtual_address);
        const JitTable::Instruction *instruction = &JitTable::instructions[code];
        if (instruction->type == JitTable::Instruction::Type_Prefix) 
        {
            code = ReadVirtualAddress(current_virtual_address + 1);
            instruction = &JitTable::cb_instructions[code];
        }

        uint32 instruction_length = instruction->length;
        if (instruction->type == JitTable::Instruction::Type_Stub)
        {
            Log_ErrorPrintf("stub instruction %02x at %05x", code, current_virtual_address);
            return nullptr;
        }

        // jump instructions
        if (instruction->type == JitTable::Instruction::Type_JR)
        {
            // jumps are always two bytes
            int8 displacement = (int8)ReadVirtualAddress(current_virtual_address + 1);
            JumpEntry entry;
            entry.JumpSource = current_real_address;
            entry.JumpTarget = (displacement < 0) ? (current_real_address - (uint8)-displacement) : (current_real_address + (uint8)displacement);
            block->Jumps.Add(entry);
        }

        Log_DevPrintf("  0x%05x: %u", current_virtual_address, instruction->type);
        last_virtual_address = current_virtual_address;
        current_real_address += (uint16)instruction_length;
        current_virtual_address += instruction_length;
        block->ByteCount += instruction_length;
        block->InstructionCount++;

        SmallString disasm;
        if (CPU::Disassemble(&disasm, m_system, block->StartRealAddress + (last_virtual_address - block->StartVirtualAddress)))
            Log_DevPrint(disasm);

        // end of block instruction?
        if (instruction->type == JitTable::Instruction::Type_JP ||
            instruction->type == JitTable::Instruction::Type_CALL ||
            instruction->type == JitTable::Instruction::Type_RET ||
            instruction->type == JitTable::Instruction::Type_RETI ||
            instruction->type == JitTable::Instruction::Type_RST ||
            instruction->type == JitTable::Instruction::Type_HALT ||
            instruction->type == JitTable::Instruction::Type_STOP ||
            instruction->type == JitTable::Instruction::Type_EI ||
            instruction->type == JitTable::Instruction::Type_DI)
        {
            break;
        }
    }

    Log_DevPrintf("----------------");
    Log_DevPrintf("end of block, %u bytes", block->ByteCount);
    return true;
}
