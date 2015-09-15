#pragma once
#include "YBaseLib/Common.h"
#include "YBaseLib/Timer.h"
#include "structures.h"

class CPU;
class Display;
class Cartridge;

class System
{
    friend CPU;
    friend Display;
    friend Cartridge;

public:
    struct CallbackInterface
    {
        // Display updated callback.
        virtual void PresentDisplayBuffer(const void *pPixels, uint32 row_stride) = 0;
    };

public:
    System();
    ~System();
    
    CPU *GetCPU() const { return m_cpu; }
    Display *GetDisplay() const { return m_display; }

    Cartridge *GetCartridge() const { return m_cartridge; }

    bool Init(CallbackInterface *callbacks, const byte *bios, Cartridge *cartridge);
    void Reset();   
    void Step();

    // Returns the number of seconds to sleep for.
    double ExecuteFrame();

    // Pad direction
    void SetPadDirection(PAD_DIRECTION direction);
    void SetPadDirection(PAD_DIRECTION direction, bool state);
    void SetPadButton(PAD_BUTTON button, bool state);

    // emulation speed multiplier
    float GetTargetSpeed() const { return m_speed_multiplier; }
    void SetTargetSpeed(float multiplier);

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
    void UpdateTimer(uint32 clocks);
    void DisassembleCart(const char *outfile);
    uint64 TimeToClocks(double time);

    CPU *m_cpu;
    Display *m_display;

    CallbackInterface *m_callbacks;
    const byte *m_bios;
    Cartridge *m_cartridge;

    Timer m_reset_timer;

    uint64 m_clocks_since_reset;
    float m_speed_multiplier;

    // bios, rom banks 0-1
    byte m_memory_vram[0x2000];
    byte m_memory_wram[0x2000];
    byte m_memory_oam[0xFF];
    byte m_memory_zram[127];

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

