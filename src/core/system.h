#pragma once

#include "common/types.h"
#include "structures.h"

#include "retro2/retro2_fwd.h"

#include <memory>
#include <span>

class Error;

class CPU;
class Display;
class Audio;
class Serial;
class Cartridge;
class StateWrapper;

class System
{
  friend CPU;
  friend Display;
  friend Audio;
  friend Cartridge;
  friend Serial;

public:
  System();
  ~System();

  static const char* GetModeName(SYSTEM_MODE mode);

  SYSTEM_MODE GetBootMode() const { return m_boot_mode; }
  SYSTEM_MODE GetCurrentMode() const { return m_current_mode; }
  bool InDMGMode() const { return (m_current_mode == SYSTEM_MODE_DMG); }
  bool InSGBMode() const { return (m_current_mode == SYSTEM_MODE_SGB); }
  bool InCGBMode() const { return (m_current_mode == SYSTEM_MODE_CGB); }

  CPU* GetCPU() const { return m_cpu; }
  Display* GetDisplay() const { return m_display; }
  Audio* GetAudio() const { return m_audio; }
  Serial* GetSerial() const { return m_serial; }

  void SetMemoryMap();

  Cartridge* GetCartridge() const { return m_cartridge.get(); }

  void UpdateSettings();

  bool Init(SYSTEM_MODE mode, const byte* bios, uint32 bios_length, std::unique_ptr<Cartridge> cartridge, Error* error);
  void Reset();
  void Step();

  // Returns the number of seconds to sleep for.
  void ExecuteFrame();

  // Pad direction
  void SetPadDirection(PAD_DIRECTION direction);
  void SetPadDirection(PAD_DIRECTION direction, bool state);
  void SetPadDirectionState(uint8 state);
  void SetPadButton(PAD_BUTTON button, bool state);
  void SetPadButtonState(uint8 state);

  // permissive memory access
  bool GetPermissiveMemoryAccess() const { return m_memory_permissive; }
  void SetPermissiveMemoryAccess(bool on) { m_memory_permissive = on; }

  // save/load savestate
  bool LoadState(std::span<const u8> buffer, Error* error);
  bool SaveState(std::span<u8> buffer, size_t* written_length, Error* error);

private:
  // cpu view of memory
  uint8 CPURead(uint16 address);
  void CPUWrite(uint16 address, uint8 value);

  // cpu io registers
  uint8 CPUReadIORegister(uint8 index);
  void CPUWriteIORegister(uint8 index, uint8 value);

  // cpu interrupt request
  void CPUInterruptRequest(uint8 index);

  // oam access from gpu
  const byte* GetOAM() const { return m_memory_oam; }
  byte* GetOAM() { return m_memory_oam; }
  void SetOAMLock(bool locked) { m_oamLocked = locked; }

  // vram access from gpu
  uint8 GetActiveCPUVRAMBank() const { return m_vram_bank; }
  const byte* GetVRAM(size_t bank) const { return m_memory_vram[bank]; }
  byte* GetVRAM(size_t bank) { return m_memory_vram[bank]; }
  void SetVRAMLock(bool locked) { m_vramLocked = locked; }

  // disable cpu
  void DisableCPU(bool disabled);

  // OAM DMA Transfer
  void OAMDMATransfer(uint16 source_address);

  // CGB Speed Switch
  bool SwitchCGBSpeed();

  // serial pause
  void SetSerialPause(bool enabled);

  // trigger OAM bug if all conditions are met
  void TriggerOAMBug();

  // synchronization
  // yeah this'll overflow, but it just means we'll synchronize too early, then correct it afterwards
  uint32 GetCycleNumber() const { return m_cycle_number; }
  uint32 GetDoubleSpeedDivider() const { return (m_cgb_speed_switch >> 7); }
  void SetNextDisplaySyncCycle(uint32 cycles)
  {
    m_next_display_sync_cycle = m_cycle_number + (cycles >> GetDoubleSpeedDivider());
    UpdateNextEventCycle();
  }
  void SetNextAudioSyncCycle(uint32 cycles)
  {
    m_next_audio_sync_cycle = m_cycle_number + (cycles >> GetDoubleSpeedDivider());
    UpdateNextEventCycle();
  }
  void SetNextSerialSyncCycle(uint32 cycles)
  {
    m_next_serial_sync_cycle = m_cycle_number + cycles;
    UpdateNextEventCycle();
  }
  void SetNextTimerSyncCycle(uint32 cycles)
  {
    m_next_timer_sync_cycle = m_cycle_number + cycles;
    UpdateNextEventCycle();
  }
  void UpdateNextEventCycle();

  // helper to calculate difference
  inline uint32 CalculateCycleCount(uint32 oldCycleNumber)
  {
    return ((oldCycleNumber > m_cycle_number) ? (0xFFFFFFFF - oldCycleNumber + m_cycle_number) :
                                                (m_cycle_number - oldCycleNumber)) >>
           GetDoubleSpeedDivider();
  }
  inline uint32 CalculateDoubleSpeedCycleCount(uint32 oldCycleNumber)
  {
    return (oldCycleNumber > m_cycle_number) ? (0xFFFFFFFF - oldCycleNumber + m_cycle_number) :
                                               (m_cycle_number - oldCycleNumber);
  }

  // execute other processors while the cpu is reading memory
  void AddCPUCycles(uint32 cpu_clocks);

  // save state
  bool DoState(StateWrapper& sw);

private:
  void ResetMemory();
  void ResetTimer();
  void ResetPad();
  void SetPostBootstrapState();
  void SynchronizeTimers();
  void ScheduleTimerSynchronization();
  // void DisassembleCart(const char* outfile);

  SYSTEM_MODE m_boot_mode;
  SYSTEM_MODE m_current_mode;
  CPU* m_cpu;
  Display* m_display;
  Audio* m_audio;
  Serial* m_serial;

  std::unique_ptr<Cartridge> m_cartridge;
  const byte* m_bios;
  uint32 m_bios_length;

  // synchronization
  uint32 m_cycle_number;
  uint32 m_last_sync_cycle;
  uint32 m_next_display_sync_cycle;
  uint32 m_next_audio_sync_cycle;
  uint32 m_next_serial_sync_cycle;
  uint32 m_next_timer_sync_cycle;
  int32 m_next_event_cycle;
  bool m_event;

  uint64 m_clocks_since_reset;
  uint64 m_last_vblank_clocks;
  bool m_serial_pause;

  // bios, rom banks 0-1
  byte m_memory_vram[2][0x2000];
  byte m_memory_wram[8][0x1000]; // 8 banks of 4KB each in CGB mode
  byte m_memory_oam[0xFF];
  byte m_memory_zram[127];
  byte m_memory_ioreg[256];
  uint8 m_vram_bank;
  uint8 m_high_wram_bank;
  uint8 m_reg_FF4C;
  uint8 m_reg_FF6C;

  // when doing DMA transfer, locked memory # cycles
  uint32 m_memory_locked_cycles;
  uint16 m_memory_locked_start;
  uint16 m_memory_locked_end;
  bool m_memory_permissive;

  // timer
  uint32 m_timer_last_cycle;
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

// Retro2 Interfaces
extern IRetro2Error R2Error;
extern IRetro2Session R2Session;
extern IRetro2Settings R2Settings;
extern IRetro2FramebufferVideo R2Video;
