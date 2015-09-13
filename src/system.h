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
    System(const byte *bios, const Cartridge *cartridge);
    ~System();
    
    const Cartridge *GetCartridge() const { return m_cartridge; }

    SDL_Surface *GetDisplaySurface() const { return m_surface; }
    void SetDisplaySurface(SDL_Window *window, SDL_Surface *surface) { m_window = window; m_surface = surface; }

    void Reset();

    // TODO: Rename to RunFrame
    void Step();

    // Pad direction
    void SetPadDirection(PAD_DIRECTION direction);
    void SetPadDirection(PAD_DIRECTION direction, bool state);
    void SetPadButton(PAD_BUTTON button, bool state);

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

    // OAM DMA Transfer
    void DMATransfer(uint16 source_address, uint16 destination_address, uint32 bytes);

private:
    void ResetMemory();
    void ResetTimer();
    void ResetPad();
    void SetPostBootstrapState();
    void CopyFrameBufferToSurface();
    void UpdateTimer(uint32 clocks);
    void DisassembleCart(const char *outfile);

    SDL_Window *m_window;
    SDL_Surface *m_surface;

    CPU *m_cpu;
    Display *m_display;

    const Cartridge *m_cartridge;
    const byte *m_bios;

    // bios, rom banks 0-1
    byte m_memory_vram[0x2000];
    byte m_memory_wram[0x2000];
    byte m_memory_oam[0xFF];
    byte m_memory_zram[127];

    // external ram
    byte *m_memory_eram;
    uint32 m_memory_eram_size;

    // timer
    uint32 m_timer_clocks;
    uint32 m_timer_divider_clocks;
    uint8 m_timer_divider;
    uint8 m_timer_counter;
    uint8 m_timer_overflow_value;
    uint8 m_timer_control;

    // pad
    uint8 m_pad_row_select;
    uint8 m_pad_direction_state;
    uint8 m_pad_button_state;

    bool m_biosLatch;
    bool m_vramLocked;
    bool m_oamLocked;
};

