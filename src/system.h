#pragma once
#include "YBaseLib/Common.h"
#include "YBaseLib/Timer.h"
#include "structures.h"

class CPU;
class Display;
class Audio;
class Cartridge;

class System
{
    friend CPU;
    friend Display;
    friend Audio;
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

    bool InSGBMode() const { return (m_mode == SYSTEM_MODE_SGB); }
    bool InCGBMode() const { return (m_mode == SYSTEM_MODE_CGB); }
    
    CPU *GetCPU() const { return m_cpu; }
    Display *GetDisplay() const { return m_display; }

    Cartridge *GetCartridge() const { return m_cartridge; }

    bool Init(CallbackInterface *callbacks, SYSTEM_MODE mode, const byte *bios, Cartridge *cartridge);
    void Reset();   
    void Step();

    // Returns the number of seconds to sleep for.
    double ExecuteFrame();

    // Pad direction
    void SetPadDirection(PAD_DIRECTION direction);
    void SetPadDirection(PAD_DIRECTION direction, bool state);
    void SetPadButton(PAD_BUTTON button, bool state);

    // frame number
    uint32 GetFrameCounter() const { return m_frame_counter; }

    // current speed
    float GetCurrentSpeed() const { return m_current_speed; }

    // emulation speed multiplier
    float GetTargetSpeed() const { return m_speed_multiplier; }
    void SetTargetSpeed(float multiplier);

    // framelimiter on/off
    bool GetFrameLimiter() const { return m_frame_limiter; }
    void SetFrameLimiter(bool on) { m_frame_limiter = on; }

    // accurate timing
    bool GetAccurateTiming() const { return m_accurate_timing; }
    void SetAccurateTiming(bool on);

    // permissive memory access
    bool GetPermissiveMemoryAccess() const { return m_memory_permissive; }
    void SetPermissiveMemoryAccess(bool on) { m_memory_permissive = on; }

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
    const byte *GetVRAM(size_t bank) const { return m_memory_vram[bank]; }
    void SetVRAMLock(bool locked) { m_vramLocked = locked; }

    // OAM DMA Transfer
    void DMATransfer(uint16 source_address, uint16 destination_address, uint32 bytes);

    // CGB Speed Switch
    void SwitchCGBSpeed();

private:
    void ResetMemory();
    void ResetTimer();
    void ResetPad();
    void SetPostBootstrapState();
    void UpdateTimer(uint32 clocks);
    void DisassembleCart(const char *outfile);
    uint64 TimeToClocks(double time);

    SYSTEM_MODE m_mode;
    CPU *m_cpu;
    Display *m_display;
    Audio *m_audio;

    CallbackInterface *m_callbacks;
    const byte *m_bios;
    Cartridge *m_cartridge;

    Timer m_reset_timer;

    uint64 m_clocks_since_reset;
    float m_speed_multiplier;
    float m_current_speed;
    uint32 m_frame_counter;
    bool m_frame_limiter;
    bool m_accurate_timing;

    // bios, rom banks 0-1
    byte m_memory_vram[2][0x2000];
    byte m_memory_wram[8][0x1000];      // 8 banks of 4KB each in CGB mode
    byte m_memory_oam[0xFF];
    byte m_memory_zram[127];
    uint8 m_vram_bank;
    uint8 m_high_wram_bank;

    // when doing DMA transfer, locked memory # cycles
    uint32 m_memory_locked_cycles;
    bool m_memory_permissive;

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

    // cgb speed switch
    uint8 m_cgb_speed_switch;

    bool m_biosLatch;
    bool m_vramLocked;
    bool m_oamLocked;
};

