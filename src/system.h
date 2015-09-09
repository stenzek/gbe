#pragma once
#include "YBaseLib/Common.h"
#include "structures.h"

class CPU;
class Display;
class Cartridge;
struct SDL_Window;
struct SDL_Surface;

class System
{
    friend CPU;
    friend Display;
    friend Cartridge;

public:
    System();
    ~System();
    
    const Cartridge *GetCartridge() const { return m_cartridge; }
    void SetCartridge(const Cartridge *cartridge) { m_cartridge = cartridge; }
    void SetBios(const byte *bios) { m_bios = bios; }

    SDL_Surface *GetDisplaySurface() const { return m_surface; }
    void SetDisplaySurface(SDL_Window *window, SDL_Surface *surface) { m_window = window; m_surface = surface; }

    void Reset();

    // TODO: Rename to RunFrame
    void Step();

private:
    // cpu view of memory
    uint8 CPURead(uint16 address) const;
    void CPUWrite(uint16 address, uint8 value);

    // cpu io registers
    uint8 CPUReadIORegister(uint8 index) const;
    void CPUWriteIORegister(uint8 index, uint8 value);

    // cpu interrupt request
    void CPUInterruptRequest(uint8 index);

    // oam access from gpu
    const byte *GetOAM() const { return m_memory_oam; }
    void SetOAMLock(bool locked) { m_oamLocked = locked; }

    // vram access from gpu
    const byte *GetVRAM() const { return m_memory_vram; }
    void SetVRAMLock(bool locked) { m_vramLocked = locked; }

private:
    void ResetMemory();
    void ResetTimer();
    void SetPostBootstrapState();
    void CopyFrameBufferToSurface();
    void UpdateTimer(uint32 cycles);

    SDL_Window *m_window;
    SDL_Surface *m_surface;

    CPU *m_cpu;
    Display *m_display;

    const Cartridge *m_cartridge;
    const byte *m_bios;

    // bios, rom banks 0-1
    //byte m_memory_ROM0[0x4000];
    //byte m_memory_ROM1[0x4000];
    const byte *m_memory_ROM0;
    const byte *m_memory_ROM1;
    byte m_memory_vram[0x2000];
    byte m_memory_eram[0x2000];
    byte m_memory_wram[0x2000];
    byte m_memory_oam[0xFF];
    byte m_memory_zram[127];

    // timer
    uint32 m_timer_cycles;
    uint8 m_timer_divider;
    uint8 m_timer_counter;
    uint8 m_timer_overflow_value;
    uint8 m_timer_control;

    bool m_biosLatch;
    bool m_vramLocked;
    bool m_oamLocked;
};

