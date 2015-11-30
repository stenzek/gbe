#include "jit_base.h"

class JitX86 : public JitBase
{
    friend class JitX86Emitter;

public:
    JitX86(System *system);
    virtual ~JitX86();

protected:
    struct X86Block : public Block
    {
        void(__cdecl *EntryPoint)(void *);
        JitX86Emitter *Emitter;
    };

    
    virtual Block *AllocateBlock(uint32 virtual_address, uint16 real_address) override final;
    virtual bool CompileBlock(Block *block) override final;
    virtual void ExecuteBlock(Block *block) override final;
    virtual void DestroyBlock(Block *block) override final;

    static void __cdecl AddCyclesTrampoline(JitX86 *this_ptr, uint32 cycles);
    static void __cdecl InterpreterFallbackTrampoline(JitX86 *this_ptr);
};

