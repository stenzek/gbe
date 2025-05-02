#include "system.h"
#include "audio.h"
#include "cartridge.h"
#include "cpu.h"
#include "display.h"
#include "serial.h"
#include "state_wrapper.h"

#include "common/assert.h"
#include "common/error.h"
#include "common/log.h"

#include <retro2/retro2_error.h>
#include <retro2/retro2_framebuffer.h>
#include <retro2/retro2_interface_loader.h>
#include <retro2/retro2_log.h>
#include <retro2/retro2_session.h>
#include <retro2/retro2_settings.h>

#include <cmath>
#include <cstring>
#include <memory>

LOG_CHANNEL(System);

IRetro2Error R2Error;
IRetro2Session R2Session;
IRetro2Settings R2Settings;
IRetro2FramebufferVideo R2Video;

// TODO: Split to separate files
static constexpr uint32 DMG_BIOS_LENGTH = 256;
static constexpr uint32 CGB_BIOS_LENGTH = 2048;

namespace {

struct SaveStateHeader
{
  u32 version;
};

static constexpr u32 SAVE_STATE_VERSION = 6;

} // namespace

System::System()
{
  m_cpu = nullptr;
  m_display = nullptr;
  m_audio = nullptr;
  m_serial = nullptr;
  m_cartridge = nullptr;
  m_bios = nullptr;
  m_bios_length = 0;
  m_biosLatch = false;
  m_vramLocked = false;
  m_oamLocked = false;
}

System::~System()
{
  delete m_serial;
  delete m_audio;
  delete m_display;
  delete m_cpu;
}

const char* System::GetModeName(SYSTEM_MODE mode)
{
  static constexpr std::array names = {
    "DMG",
    "SGB",
    "CGB",
  };
  static_assert(names.size() == NUM_SYSTEM_MODES);
  return (static_cast<u32>(mode) >= names.size()) ? names[static_cast<u32>(mode)] : "";
}

void System::UpdateSettings()
{
  m_memory_permissive = R2Settings.GetDefaultBoolValue("PermissiveMemoryAccess", true);
}

bool System::Init(SYSTEM_MODE mode, const byte* bios, uint32 bios_length, std::unique_ptr<Cartridge> cartridge,
                  Error* error)
{
  UpdateSettings();

  m_boot_mode = (mode == NUM_SYSTEM_MODES) ? cartridge->GetSystemMode() : mode;
  m_current_mode = m_boot_mode;
  m_bios = bios;
  m_bios_length = (bios != nullptr) ? bios_length : 0;
  m_cartridge = std::move(cartridge);
  if (m_bios_length != 0)
  {
    if ((m_current_mode == SYSTEM_MODE_DMG && m_bios_length != DMG_BIOS_LENGTH) ||
        (m_current_mode == SYSTEM_MODE_CGB && m_bios_length != CGB_BIOS_LENGTH))
    {
      Error::SetStringView(error, "Incorrect bootstrap rom length");
      return false;
    }
  }

  m_cpu = new CPU(this);
  m_display = new Display(this);
  m_audio = new Audio(this);
  m_serial = new Serial(this);

  m_cycle_number = 0;
  m_last_sync_cycle = 0;
  m_next_display_sync_cycle = 0;
  m_next_audio_sync_cycle = 0;
  m_next_serial_sync_cycle = 0;
  m_next_timer_sync_cycle = 0;
  m_next_event_cycle = 0;
  m_event = false;

  m_clocks_since_reset = 0;
  m_last_vblank_clocks = 0;

  m_serial_pause = false;

  m_memory_locked_cycles = 0;
  m_memory_locked_start = 0;
  m_memory_locked_end = 0;

  m_high_wram_bank = 1;
  m_vram_bank = 0;
  m_cgb_speed_switch = 0;

  // init modules
  m_cpu->Reset();
  m_display->Reset();
  m_audio->Reset();
  m_serial->Reset();

  // clear our memory
  ResetMemory();
  ResetTimer();
  ResetPad();
  if (m_cartridge != nullptr)
    m_cartridge->Reset();

  // if bios not provided, emulate post-bootstrap state
  if (m_bios == nullptr)
    SetPostBootstrapState();

  INFO_LOG("Initialized system in mode {}.", GetModeName(m_current_mode));
  return true;
}

void System::Reset()
{
  m_current_mode = m_boot_mode;

  m_cycle_number = 0;
  m_last_sync_cycle = 0;
  m_next_display_sync_cycle = 0;
  m_next_audio_sync_cycle = 0;
  m_next_serial_sync_cycle = 0;
  m_next_timer_sync_cycle = 0;
  m_next_event_cycle = 0;
  m_event = false;

  m_clocks_since_reset = 0;
  m_last_vblank_clocks = 0;

  m_memory_locked_cycles = 0;

  m_high_wram_bank = 1;
  m_vram_bank = 0;
  m_cgb_speed_switch = 0;

  m_cpu->Reset();
  m_display->Reset();
  m_audio->Reset();
  m_serial->Reset();
  ResetMemory();
  ResetTimer();
  ResetPad();
  if (m_cartridge != nullptr)
    m_cartridge->Reset();

  // if bios not provided, emulate post-bootstrap state
  if (m_bios == nullptr)
    SetPostBootstrapState();

  INFO_LOG("System reset.");
}

void System::Step()
{
  // handle serial pause
  if (m_serial_pause)
  {
    m_serial->Synchronize();
    return;
  }

  m_cpu->ExecuteInstruction();
}

void System::UpdateNextEventCycle()
{
  if (m_event)
    return;

  // these results can underflow, but the results will be correct
  uint32 cycles_to_timer_sync = m_next_timer_sync_cycle - m_cycle_number;
  uint32 cycles_to_serial_sync = m_next_serial_sync_cycle - m_cycle_number;
  uint32 cycles_to_audio_sync = m_next_audio_sync_cycle - m_cycle_number;
  uint32 cycles_to_display_sync = m_next_display_sync_cycle - m_cycle_number;

  // find the lowest sync cycle
  uint32 cycles_to_first_sync = std::min(
    cycles_to_timer_sync, std::min(cycles_to_serial_sync, std::min(cycles_to_audio_sync, cycles_to_display_sync)));
  if (m_memory_locked_cycles > 0)
    cycles_to_first_sync = std::min(cycles_to_first_sync, m_memory_locked_cycles);

  m_next_event_cycle = (int32)cycles_to_first_sync;
}

void System::AddCPUCycles(uint32 cpu_clocks)
{
  // CPU clocks are always dividable by 4
  DebugAssert((cpu_clocks % 4) == 0);
  m_cycle_number += cpu_clocks;
  m_clocks_since_reset += (cpu_clocks >> GetDoubleSpeedDivider());
  m_next_event_cycle -= (int32)cpu_clocks;
  if (m_next_event_cycle > 0)
    return;

  // what we will synchronize
  // when (m_cycle_number + next_sync) overflows, these expressions will keep returning true,
  // until m_cycle_number overflows as well. certainly not ideal due to the slowdown.
  // however, due to the downcount above, we should not be triggering often
  bool sync_timers = (m_cycle_number >= m_next_timer_sync_cycle);
  bool sync_serial = (m_cycle_number >= m_next_serial_sync_cycle);
  bool sync_display = (m_cycle_number >= m_next_display_sync_cycle);
  bool sync_audio = (m_cycle_number >= m_next_audio_sync_cycle);
  uint32 cycles_since_sync = CalculateDoubleSpeedCycleCount(m_last_sync_cycle);
  m_last_sync_cycle = m_cycle_number;
  m_event = true;

  // Handle memory locking for OAM transfers [affected by double speed]
  if (m_memory_locked_cycles > 0)
    m_memory_locked_cycles =
      (cycles_since_sync > m_memory_locked_cycles) ? 0 : (m_memory_locked_cycles - cycles_since_sync);

  // Simulate display [not affected by double speed]
  if (sync_display)
    m_display->Synchronize();

  // Simulate audio [not affected by double speed]
  if (sync_audio)
    m_audio->Synchronize();

  // Simulate serial [affected by double speed]
  if (sync_serial)
    m_serial->Synchronize();

  // Simulate timers [affected by double speed]
  if (sync_timers)
    SynchronizeTimers();

  // Update time to next event
  m_event = false;
  UpdateNextEventCycle();
}

void System::SetSerialPause(bool enabled)
{
  if (m_serial_pause == enabled)
    return;

  m_serial_pause = enabled;
  if (!m_serial_pause)
  {
    m_clocks_since_reset = 0;
    m_last_vblank_clocks = 0;
  }
}

void System::TriggerOAMBug()
{
  if (m_current_mode == SYSTEM_MODE_DMG && m_display->CanTriggerOAMBug())
  {
    // TODO: Should actually be moving OAM memory around, not random data.
    static const byte junk[152] = {
      0xCF, 0x93, 0xA1, 0x77, 0x90, 0x6B, 0xEC, 0xF2, 0xA7, 0xF4, 0x3C, 0xEF, 0x95, 0x34, 0xBD, 0x2C, 0x4F, 0x73, 0x75,
      0x01, 0x61, 0x1C, 0x91, 0xFC, 0xE8, 0x0C, 0x03, 0x06, 0xB1, 0x2F, 0xB3, 0x4D, 0xA4, 0x14, 0xB1, 0xEA, 0x29, 0xEC,
      0x21, 0x59, 0x4D, 0xF4, 0x41, 0x10, 0xA1, 0xDF, 0x51, 0x79, 0x1F, 0x5D, 0xD7, 0x1C, 0x1D, 0xC1, 0xEC, 0x0D, 0xD6,
      0xA3, 0xA7, 0x23, 0x33, 0xFC, 0x29, 0x07, 0xB5, 0xD9, 0x7F, 0x98, 0xE9, 0x5C, 0x5E, 0x8C, 0x66, 0x11, 0xEB, 0xE7,
      0xAB, 0x5F, 0x0C, 0x92, 0x3E, 0xA2, 0x92, 0x1F, 0x44, 0x42, 0xA5, 0x83, 0x57, 0x16, 0x0F, 0x4C, 0xD0, 0x60, 0xB9,
      0x26, 0xB6, 0x77, 0x2E, 0x16, 0x98, 0xAA, 0x20, 0x03, 0x81, 0x67, 0xA0, 0x0F, 0x7B, 0xBF, 0xD7, 0xD6, 0x88, 0x65,
      0xCF, 0x21, 0xE2, 0x44, 0xD0, 0x89, 0xD4, 0x4A, 0xC9, 0x03, 0x7D, 0x87, 0x87, 0x55, 0xAE, 0xFB, 0xDC, 0x3B, 0x23,
      0xC2, 0x2D, 0x78, 0x28, 0x24, 0xB1, 0xF5, 0xAC, 0xAC, 0xA5, 0x34, 0x30, 0x41, 0x8B, 0x2E, 0xAF, 0x4B, 0xBB, 0x9F};

    std::memcpy(m_memory_oam + 8, junk, sizeof(junk));
    // Log_WarningPrintf("OAM bug invoked");
  }
}

void System::ExecuteFrame()
{
  if (m_serial_pause)
  {
    m_serial->Synchronize();
    return;
  }

  uint64 cycles_executed = 0;
  uint64 last_vblank_clocks = m_last_vblank_clocks;

  // If the display is turned off, this loop will never exit.
  // Run a maximum of two vblank intervals worth of cycles in this case.
  while (cycles_executed < (70224 * 2) && last_vblank_clocks == m_last_vblank_clocks && !m_serial_pause)
    Step();
}

void System::SetPadDirection(PAD_DIRECTION direction)
{
  uint8 old_direction_state = m_pad_direction_state;
  m_pad_direction_state = ~(PAD_DIRECTION_MASK & direction) & PAD_DIRECTION_MASK;
  if (old_direction_state != m_pad_direction_state)
  {
    TRACE_LOG("Pad direction set to 0x{:02X}", static_cast<u8>(direction));
    CPUInterruptRequest(CPU_INT_JOYPAD);
  }
}

void System::SetPadDirection(PAD_DIRECTION direction, bool state)
{
  uint8 old_direction_state = m_pad_direction_state;

  if (!state)
    m_pad_direction_state |= 0xF & direction;
  else
    m_pad_direction_state &= ~(0xF & direction);

  if (old_direction_state != m_pad_direction_state)
  {
    TRACE_LOG("Pad direction 0x{:02X} set {}", static_cast<u8>(direction), state ? "on" : "off");
    CPUInterruptRequest(CPU_INT_JOYPAD);
  }
}

void System::SetPadDirectionState(uint8 state)
{
  // flip on bits to off (which is what the gb expects)
  state = (state & PAD_DIRECTION_MASK) ^ PAD_DIRECTION_MASK;
  if (m_pad_direction_state == state)
    return;

  m_pad_direction_state = state;
  CPUInterruptRequest(CPU_INT_JOYPAD);
}

void System::SetPadButton(PAD_BUTTON button, bool state)
{
  uint8 old_button_state = m_pad_button_state;

  if (!state)
    m_pad_button_state |= 0xF & button;
  else
    m_pad_button_state &= ~(0xF & button);

  if (old_button_state != m_pad_button_state)
  {
    TRACE_LOG("Pad button 0x{:02X} set {}", static_cast<u8>(button), state ? "on" : "off");
    CPUInterruptRequest(CPU_INT_JOYPAD);
  }
}

void System::SetPadButtonState(uint8 state)
{
  // flip on bits to off (which is what the gb expects)
  state = (state & PAD_BUTTON_MASK) ^ PAD_BUTTON_MASK;
  if (m_pad_button_state == state)
    return;

  m_pad_button_state = state;
  CPUInterruptRequest(CPU_INT_JOYPAD);
}

bool System::LoadState(std::span<const u8> buffer, Error* error)
{
  if (buffer.size() < sizeof(SaveStateHeader))
  {
    Error::SetStringView(error, "Invalid buffer size.");
    return false;
  }

  SaveStateHeader header;
  std::memcpy(&header, buffer.data(), sizeof(header));
  if (header.version != SAVE_STATE_VERSION)
  {
    Error::SetStringFmt(error, "Save state version mismatch, expected {}, got {}", SAVE_STATE_VERSION, header.version);
    return false;
  }

  StateWrapper sw(buffer.subspan(sizeof(header)), StateWrapper::Mode::Read, header.version);
  if (!DoState(sw))
  {
    Error::SetStringView(error, "Save state data is corrupted.");
    return false;
  }

  return true;
}

bool System::SaveState(std::span<u8> buffer, size_t* written_length, Error* error)
{
  if (buffer.size() < sizeof(SaveStateHeader))
  {
    Error::SetStringView(error, "Invalid buffer size.");
    return false;
  }

  const SaveStateHeader header{.version = SAVE_STATE_VERSION};
  std::memcpy(buffer.data(), &header, sizeof(header));

  StateWrapper sw(buffer.subspan(sizeof(header)), StateWrapper::Mode::Write, header.version);
  if (!DoState(sw))
  {
    Error::SetStringView(error, "Out of buffer space.");
    return false;
  }

  *written_length = sizeof(header) + sw.GetPosition();
  return true;
}

bool System::DoState(StateWrapper& sw)
{
  // Write state
  sw.Do(&m_boot_mode);
  sw.Do(&m_current_mode);
  sw.Do(&m_cycle_number);
  sw.Do(&m_last_sync_cycle);
  sw.Do(&m_next_display_sync_cycle);
  sw.Do(&m_next_audio_sync_cycle);
  sw.Do(&m_next_serial_sync_cycle);
  sw.Do(&m_next_timer_sync_cycle);
  sw.Do(&m_next_event_cycle);
  sw.Do(&m_event);
  sw.Do(&m_clocks_since_reset);
  sw.Do(&m_last_vblank_clocks);
  sw.Do(&m_serial_pause);

  // Write memory
  sw.DoBytes(m_memory_vram, sizeof(m_memory_vram));
  sw.DoBytes(m_memory_wram, sizeof(m_memory_wram));
  sw.DoBytes(m_memory_oam, sizeof(m_memory_oam));
  sw.DoBytes(m_memory_zram, sizeof(m_memory_zram));

  // Write registers
  sw.Do(&m_vram_bank);
  sw.Do(&m_high_wram_bank);
  sw.Do(&m_reg_FF4C);
  sw.Do(&m_reg_FF6C);

  sw.Do(&m_memory_locked_cycles);
  sw.Do(&m_memory_locked_start);
  sw.Do(&m_memory_locked_end);

  sw.Do(&m_timer_last_cycle);
  sw.Do(&m_timer_clocks);
  sw.Do(&m_timer_divider_clocks);
  sw.Do(&m_timer_divider);
  sw.Do(&m_timer_counter);
  sw.Do(&m_timer_overflow_value);
  sw.Do(&m_timer_control);

  sw.Do(&m_pad_row_select);
  // sw.Do(&m_pad_direction_state);
  // sw.Do(&m_pad_button_state);

  sw.Do(&m_cgb_speed_switch);

  sw.Do(&m_biosLatch);
  sw.Do(&m_vramLocked);
  sw.Do(&m_oamLocked);

  if (sw.HasError())
    return false;

  // Write Cartridge state
  if (!m_cartridge->DoState(sw))
    return false;

  // Write CPU state
  if (!m_cpu->DoState(sw))
    return false;

  // Write Display state
  if (!m_display->DoState(sw))
    return false;

  // Write Audio state
  if (!m_audio->DoState(sw))
    return false;

  // Write Serial state
  if (!m_serial->DoState(sw))
    return false;

  return true;
}

void System::DisableCPU(bool disabled)
{
  m_cpu->Disable(disabled);
}

void System::OAMDMATransfer(uint16 source_address)
{
  m_memory_locked_cycles = 0;

  // select locked memory range
  switch (source_address & 0xF000)
  {
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
      m_memory_locked_start = 0x0000;
      m_memory_locked_end = 0x3FFF;
      break;

    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
      m_memory_locked_start = 0x4000;
      m_memory_locked_end = 0x7FFF;
      break;

    case 0xA000:
    case 0xB000:
      m_memory_locked_start = 0xA000;
      m_memory_locked_end = 0xBFFF;
      break;

    case 0xC000:
      m_memory_locked_start = 0xC000;
      m_memory_locked_end = 0xCFFF;
      break;

    case 0xD000:
      m_memory_locked_start = 0xD000;
      m_memory_locked_end = 0xDFFF;
      break;

    case 0xE000:
      // TODO: This is shadow of C000-CFFF
      m_memory_locked_start = 0xE000;
      m_memory_locked_end = 0xEFFF;
      break;

    case 0xF000:
    {
      if (source_address < 0xFE00)
      {
        m_memory_locked_start = 0xF000;
        m_memory_locked_end = 0xFD99;
      }
    }
    break;
  }

  // Allow transfers from vram regardless of locking state.
  // Is this correct?
  bool vramLocked = m_vramLocked;

  if (source_address == 0xFE00)
  {
    // OAM-OAM - ignore?
    WARNING_LOG("DMA transfer from OAM-OAM");
  }
  else if (source_address == 0xFF00)
  {
    // MMIO/ZRAM->OAM - copy zeros?
    for (uint32 i = 0; i < 160; i++)
      m_memory_oam[i] = 0;
  }
  else
  {
    // slow but due to the ranges has to be done this way
    // TODO: break this up for reads/writes when in progress
    for (uint32 i = 0; i < 160; i++)
      m_memory_oam[i] = CPURead(source_address + (uint16)i);
  }

  // Stall memory access for ~160 microseconds
  m_vramLocked = vramLocked;
  m_memory_locked_cycles = 640;
  UpdateNextEventCycle();
}

bool System::SwitchCGBSpeed()
{
  if (!(m_cgb_speed_switch & (1 << 0)))
    return false;

  // synchronize all clocks at the current clock speed
  m_display->Synchronize();
  m_audio->Synchronize();
  m_serial->Synchronize();
  SynchronizeTimers();

  // Flips the switch bit off at the same time.
  m_cgb_speed_switch ^= 0x81;
  if (m_cgb_speed_switch & 0x80)
    DEV_LOG("Switching to CGB double speed mode.");
  else
    DEV_LOG("Switching to normal speed mode.");

  // reset timing - so that accurate timing doesn't break
  // m_clocks_since_reset = 0;
  // m_reset_timer.Reset();

  // re-synchronize all clocks again to fix the cycle counters
  m_display->Synchronize();
  m_audio->Synchronize();
  m_serial->Synchronize();
  SynchronizeTimers();
  UpdateNextEventCycle();
  return true;
}

void System::ResetMemory()
{
  // bios is initially mapped
  m_biosLatch = true;

  // zero all memory
  std::memset(m_memory_vram, 0, sizeof(m_memory_vram));
  std::memset(m_memory_wram, 0, sizeof(m_memory_wram));
  std::memset(m_memory_oam, 0, sizeof(m_memory_oam));
  std::memset(m_memory_zram, 0, sizeof(m_memory_zram));
  std::memset(m_memory_ioreg, 0, sizeof(m_memory_ioreg));

  // pad
  m_pad_row_select = 0;
}

void System::ResetTimer()
{
  m_timer_last_cycle = 0;
  m_timer_clocks = 0;
  m_timer_divider_clocks = 0;
  m_timer_divider = 1;
  m_timer_counter = 0;
  m_timer_overflow_value = 0;
  m_timer_control = 0;
}

void System::ResetPad()
{
  m_pad_row_select = 0x30;      // neither selected
  m_pad_direction_state = 0x0F; // nothing down
  m_pad_button_state = 0x0F;    // nothing down
}

void System::SetPostBootstrapState()
{
  // http://bgb.bircd.org/pandocs.txt -> Power Up Sequence
  CPU::Registers* registers = m_cpu->GetRegisters();
  registers->AF = (InCGBMode()) ? 0x11B0 : 0x01B0;
  registers->BC = 0x0013;
  registers->DE = 0x00D8;
  registers->HL = 0x014D;
  registers->SP = 0xFFFE;
  registers->PC = 0x0100;

  CPUWriteIORegister(0x05, 0x00);                        // TIMA
  CPUWriteIORegister(0x06, 0x00);                        // TMA
  CPUWriteIORegister(0x07, 0x00);                        // TAC
  CPUWriteIORegister(0x10, 0x80);                        // NR10
  CPUWriteIORegister(0x11, 0xBF);                        // NR11
  CPUWriteIORegister(0x12, 0xF3);                        // NR12
  CPUWriteIORegister(0x14, 0xBF);                        // NR14
  CPUWriteIORegister(0x16, 0x3F);                        // NR21
  CPUWriteIORegister(0x17, 0x00);                        // NR22
  CPUWriteIORegister(0x19, 0xBF);                        // NR24
  CPUWriteIORegister(0x1A, 0x7F);                        // NR30
  CPUWriteIORegister(0x1B, 0xFF);                        // NR31
  CPUWriteIORegister(0x1C, 0x9F);                        // NR32
  CPUWriteIORegister(0x1E, 0xBF);                        // NR33
  CPUWriteIORegister(0x20, 0xFF);                        // NR41
  CPUWriteIORegister(0x21, 0x00);                        // NR42
  CPUWriteIORegister(0x22, 0x00);                        // NR43
  CPUWriteIORegister(0x23, 0xBF);                        // NR30
  CPUWriteIORegister(0x24, 0x77);                        // NR50
  CPUWriteIORegister(0x25, 0xF3);                        // NR51
  CPUWriteIORegister(0x26, (InSGBMode()) ? 0xF0 : 0xF1); // NR52 (F0 on SGB)
  CPUWriteIORegister(0x40, 0x91);                        // LCDC
  CPUWriteIORegister(0x42, 0x00);                        // SCY
  CPUWriteIORegister(0x43, 0x00);                        // SCX
  CPUWriteIORegister(0x45, 0x00);                        // LYC
  CPUWriteIORegister(0x47, 0xFC);                        // BGP
  CPUWriteIORegister(0x48, 0xFF);                        // OBP0
  CPUWriteIORegister(0x49, 0xFF);                        // OBP1
  CPUWriteIORegister(0x4A, 0x00);                        // WY
  CPUWriteIORegister(0x4B, 0x00);                        // WX
  CPUWriteIORegister(0xFF, 0x00);                        // IE

  m_biosLatch = false;
}

void System::SynchronizeTimers()
{
  uint32 cycles_to_execute = CalculateDoubleSpeedCycleCount(m_timer_last_cycle);
  m_timer_last_cycle = GetCycleNumber();

  // cpu runs at 4,194,304hz
  // timer runs at 16,384hz
  // therefore, every 256 cpu "clocks" equals one timer tick
  m_timer_divider_clocks += cycles_to_execute;
  while (m_timer_divider_clocks >= 256)
  {
    m_timer_divider++;
    m_timer_divider_clocks -= 256;
  }

  // timer start/stop
  if (m_timer_control & 0x4)
  {
    // add cycles
    m_timer_clocks += cycles_to_execute;

    // find timer rate
    // static const uint32 clock_rates[] = { 4096, 262144, 65536, 16384 };
    static const uint32 clocks_per_timer_ticks[] = {1024, 16, 64, 256};
    uint32 clocks_per_timer_tick = clocks_per_timer_ticks[m_timer_control & 0x3];

    // cap at one iteration when the rate changes
    // if (m_timer_control_changed && m_timer_cycles > clock_rate)
    // m_timer_cycles %= clock_rate;

    // increment timer
    while (m_timer_clocks >= clocks_per_timer_tick)
    {
      if ((++m_timer_counter) == 0x00)
      {
        // timer overflow
        CPUInterruptRequest(CPU_INT_TIMER);
        m_timer_counter = m_timer_overflow_value;
      }

      m_timer_clocks -= clocks_per_timer_tick;
    }
  }

  ScheduleTimerSynchronization();
}

void System::ScheduleTimerSynchronization()
{
  if (m_timer_control & 0x4)
  {
    // schedule update for the next interrupt time
    static const uint32 clocks_per_timer_ticks[] = {1024, 16, 64, 256};
    uint32 clocks_per_timer_tick = clocks_per_timer_ticks[m_timer_control & 0x3];
    uint32 next_interrupt_time = (256 - m_timer_counter) * clocks_per_timer_tick - m_timer_clocks;
    SetNextTimerSyncCycle(next_interrupt_time);
  }
  else
  {
    // divider timer is updated on-demand when it is read, so just set to +1 sec
    SetNextTimerSyncCycle(4194304);
  }
}

#if 0
void System::DisassembleCart(const char* outfile)
{
  ByteStream* pStream =
    FileSystem::OpenFile(outfile, BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_WRITE | BYTESTREAM_OPEN_TRUNCATE);
  if (pStream == nullptr)
    return;

  CPU::DisassembleFrom(this, 0x0000, 0x8000, pStream);
  pStream->Release();
}
#endif

uint8 System::CPURead(uint16 address)
{
  //     if (address == 0xc009)
  //         __debugbreak();

  // when DMA transfer is in progress, all memory except FF80-FFFE is inaccessible
  if (m_memory_locked_cycles > 0 && !m_memory_permissive && address >= m_memory_locked_start &&
      address <= m_memory_locked_end) [[unlikely]]
  {
    // TODO: Should change the currently-buffered byte in the DMA transfer
    DEV_LOG("WARN: CPU read of address 0x{:04X} denied during DMA transfer", address);
    return 0x00;
  }

  // select address range
  switch (address & 0xF000)
  {
      // cart memory
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
    case 0xA000:
    case 0xB000:
    {
      if (m_biosLatch)
      {
        // DMG rom is 256 bytes from 0000->00FF
        // CGB rom is 256 bytes from 0000->00FF, 0200->08FF
        if (m_current_mode == SYSTEM_MODE_DMG)
        {
          if (address <= 0x00FF)
            return m_bios[address];
        }
        else if (m_current_mode == SYSTEM_MODE_CGB)
        {
          if (address <= 0x00FF)
            return m_bios[address];
          else if (address >= 0x0200 && address <= 0x08FF)
            return m_bios[0x0100 + (address - 0x0200)];
        }
      }

      // Cart read
      return (m_cartridge != nullptr) ? m_cartridge->CPURead(address) : 0x00;
    }

      // video memory
    case 0x8000:
    case 0x9000:
    {
      m_display->Synchronize();
      if (m_vramLocked && !m_memory_permissive) [[unlikely]]
      {
        // Apparently returns 0xFF?
        DEV_LOG("WARN: CPU read of VRAM address 0x{:04X} while locked.", address);
        return 0xFF;
      }

      return m_memory_vram[m_vram_bank][address & 0x1FFF];
    }

      // working ram
    case 0xC000:
      return m_memory_wram[0][address & 0xFFF];

    case 0xD000:
      return m_memory_wram[m_high_wram_bank][address & 0xFFF];

      // working ram shadow
    case 0xE000:
      return m_memory_wram[0][address & 0xFFF];

      // working ram shadow, i/o, zero-page
    case 0xF000:
    {
      switch (address & 0x0F00)
      {
          // working ram shadow
        case 0x000:
        case 0x100:
        case 0x200:
        case 0x300:
        case 0x400:
        case 0x500:
        case 0x600:
        case 0x700:
        case 0x800:
        case 0x900:
        case 0xA00:
        case 0xB00:
        case 0xC00:
        case 0xD00:
          return m_memory_wram[m_high_wram_bank][address & 0xFFF];

          // oam
        case 0xE00:
        {
          m_display->Synchronize();
          if (m_oamLocked && !m_memory_permissive) [[unlikely]]
          {
            // Apparently returns 0xFF?
            DEV_LOG("WARN: CPU read of OAM address 0x{:04X} while locked.", address);
            return 0xFF;
          }
          else if (address >= 0xFEA0)
          {
            DEV_LOG("WARN: Out-of-range read of OAM address 0x{:04X}", address);
            return 0x00;
          }

          return m_memory_oam[address & 0xFF];
        }

          // zero page
        case 0xF00:
        {
          if (address >= 0xFF80 && address < 0xFFFF)
          {
            // fast ram
            return m_memory_zram[address - 0xFF80];
          }
          else
          {
            // IO registers, slow access
            return CPUReadIORegister(address & 0xFF);
          }
        }
      }
    }
  }

  // unhandled read
  WARNING_LOG("Unhandled CPU read address 0x{:04X}", address);
  return 0x00;
}

void System::CPUWrite(uint16 address, uint8 value)
{
  //     if (address == 0xd000)
  //         __debugbreak();

  // when DMA transfer is in progress, all memory except FF80-FFFE is inaccessible
  if (m_memory_locked_cycles > 0 && !m_memory_permissive && address >= m_memory_locked_start &&
      address <= m_memory_locked_end) [[unlikely]]
  {
    DEV_LOG("WARN: CPU write of address 0x{:04X} (value 0x{:02X}) denied during DMA transfer", address, value);
    return;
  }

  // select memory range
  switch (address & 0xF000)
  {
      // cart memory
    case 0x0000:
    case 0x1000:
    case 0x2000:
    case 0x3000:
    case 0x4000:
    case 0x5000:
    case 0x6000:
    case 0x7000:
    case 0xA000:
    case 0xB000:
    {
      if (m_cartridge != nullptr)
        m_cartridge->CPUWrite(address, value);

      return;
    }

      // vram
    case 0x8000:
    case 0x9000:
    {
      m_display->Synchronize();
      if (m_vramLocked && !m_memory_permissive) [[unlikely]]
      {
        DEV_LOG("WARN: CPU write of VRAM address 0x{:04X} (value 0x{:02X}) while locked.", address, value);
        return;
      }

      //             if (address >= 0x9800 && m_vram_bank == 1)
      //             {
      //                 uint32 tx = (address - 0x9800) % 32;
      //                 uint32 ty = (address - 0x9800) / 32;
      //                 Log_DevPrintf("tile update: (%u, %u) -> %u", tx, ty, value);
      //             }

      m_memory_vram[m_vram_bank][address & 0x1FFF] = value;
      return;
    }

      // working ram
    case 0xC000:
      m_memory_wram[0][address & 0xFFF] = value;
      return;

    case 0xD000:
      m_memory_wram[m_high_wram_bank][address & 0xFFF] = value;
      return;

      // working ram shadow
    case 0xE000:
      m_memory_wram[0][address & 0xFFF] = value;
      return;

      // working ram shadow, i/o, zero-page
    case 0xF000:
    {
      switch (address & 0x0F00)
      {
          // working ram shadow
        case 0x000:
        case 0x100:
        case 0x200:
        case 0x300:
        case 0x400:
        case 0x500:
        case 0x600:
        case 0x700:
        case 0x800:
        case 0x900:
        case 0xA00:
        case 0xB00:
        case 0xC00:
        case 0xD00:
          m_memory_wram[m_high_wram_bank][address & 0xFFF] = value;
          return;

          // oam
        case 0xE00:
        {
          m_display->Synchronize();
          if (m_oamLocked && !m_memory_permissive) [[unlikely]]
          {
            // Apparently returns 0xFF?
            DEV_LOG("WARN: CPU write of OAM address 0x{:04X} (value 0x{:02X}) while locked.", address, value);
            return;
          }

          if (address >= 0xFEA0) [[unlikely]]
          {
            DEV_LOG("WARN: Out-of-range write of OAM address 0x{:04X} (value 0x{:02X})", address, value);
            return;
          }

          m_memory_oam[address & 0xFF] = value;
          return;
        }

          // zero page
        case 0xF00:
        {
          if (address >= 0xFF80 && address < 0xFFFF)
          {
            // fast ram
            m_memory_zram[address - 0xFF80] = value;
            return;
          }
          else
          {
            // IO registers, slow access
            CPUWriteIORegister(address & 0xFF, value);
            return;
          }
        }
      }
    }
  }

  // unhandled write
  WARNING_LOG("Unhandled CPU write address 0x{:04X} (value 0x{:02X})", address, value);
}

uint8 System::CPUReadIORegister(uint8 index)
{
  switch (index & 0xF0)
  {
    case 0x00:
    {
      switch (index & 0x0F)
      {
          // Joypad
        case 0x00:
        {
          if ((m_pad_row_select & 0x10) == 0)
            return m_pad_row_select | m_pad_direction_state;
          else if ((m_pad_row_select & 0x20) == 0)
            return m_pad_row_select | m_pad_button_state;

          return m_pad_row_select | 0x0F;
        }

          // FF01 - SB serial data
        case 0x01:
          m_serial->Synchronize();
          return m_serial->GetSerialData();

          // FF02 - SC serial control
        case 0x02:
          m_serial->Synchronize();
          return m_serial->GetSerialControl();

          // FF04 - DIV - Divider Register (R/W)
        case 0x04:
          SynchronizeTimers();
          return m_timer_divider;

          // FF05 - TIMA - Timer counter (R/W)
        case 0x05:
          SynchronizeTimers();
          return m_timer_counter;

          // FF06 - TMA - Timer Modulo (R/W)
        case 0x06:
          SynchronizeTimers();
          return m_timer_overflow_value;

          // FF07 - TAC - Timer Control (R/W)
        case 0x07:
          SynchronizeTimers();
          return m_timer_control;

          // FF0F - IF - Interrupt Flag (R/W)
        case 0x0F:
          return m_cpu->GetRegisters()->IF;
      }

      break;
    }

    case 0x10:
    {
      switch (index & 0x0F)
      {
        case 0x00: // FF10 - NR10 - Channel 1 Sweep register (R/W)
        case 0x01: // FF11 - NR11 - Channel 1 Sound length/Wave pattern duty (R/W)
        case 0x02: // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
        case 0x03: // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
        case 0x04: // FF14 - NR14 - Channel 1 Frequency hi (R/W)
        case 0x05: // FF15 - NR?? - ???
        case 0x06: // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
        case 0x07: // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
        case 0x08: // FF18 - NR23 - Channel 2 Frequency lo data (W)
        case 0x09: // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
        case 0x0A: // FF1A - NR30 - Channel 3 Sound on/off (R/W)
        case 0x0B: // FF1B - NR31 - Channel 3 Sound Length
        case 0x0C: // FF1C - NR32 - Channel 3 Select output level (R/W)
        case 0x0D: // FF1D - NR33 - Channel 3 Frequency's lower data (W)
        case 0x0E: // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
        case 0x0F: // FF1F - NR?? - ???
          m_audio->Synchronize();
          return m_audio->CPUReadRegister(index);
      }

      break;
    }

    case 0x20:
    {
      switch (index & 0x0F)
      {
        case 0x00: // FF20 - NR41 - Channel 4 Sound Length (R/W)
        case 0x01: // FF21 - NR42 - Channel 4 Volume Envelope (R/W)
        case 0x02: // FF22 - NR43 - Channel 4 Polynomial Counter (R/W)
        case 0x03: // FF23 - NR44 - Channel 4 Counter/consecutive; Inital (R/W)
        case 0x04: // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
        case 0x05: // FF25 - NR51 - Selection of Sound output terminal (R/W)
        case 0x06: // FF26 - NR52 - sound on/off
        case 0x07: // FF27 - ???
        case 0x08: // FF28 - ???
        case 0x09: // FF29 - ???
        case 0x0A: // FF2A - ???
        case 0x0B: // FF2B - ???
        case 0x0C: // FF2C - ???
        case 0x0D: // FF2D - ???
        case 0x0E: // FF2E - ???
        case 0x0F: // FF2F - ???
          m_audio->Synchronize();
          return m_audio->CPUReadRegister(index);
      }

      break;
    }

    case 0x30:
    {
      // FF30-FF3F - Wave Pattern RAM
      m_audio->Synchronize();
      return m_audio->CPUReadRegister(index);
    }

    case 0x40:
    {
      // LCD registers
      switch (index & 0x0F)
      {
        case 0x00: // FF40 - LCDC - LCD Control (R/W)
        case 0x01: // FF41 - STAT - LCDC Status (R/W)
        case 0x02: // FF42 - SCY - Scroll Y (R/W)
        case 0x03: // FF43 - SCX - Scroll X (R/W)
        case 0x04: // FF44 - LY - LCDC Y-Coordinate (R/W?)
        case 0x05: // FF45 - LYC - LY Compare(R / W)
        case 0x07: // FF47 - BGP - BG Palette Data (R/W) - Non CGB Mode Only
        case 0x08: // FF48 - OBP0 - Object Palette 0 Data (R/W) - Non CGB Mode Only
        case 0x09: // FF49 - OBP1 - Object Palette 1 Data(R / W) - Non CGB Mode Only
        case 0x0A: // FF4A - WY - Window Y Position (R/W)
        case 0x0B: // FF4B - WX - Window X Position minus 7 (R/W)
          m_display->Synchronize();
          return m_display->CPUReadRegister(index);
      }

      break;
    }

    case 0x50:
    {
      // LCD registers
      switch (index & 0x0F)
      {
        case 0x00: // FF50 - BIOS latch
          return m_biosLatch;
      }

      break;
    }

    case 0xF0:
    {
      switch (index & 0x0F)
      {
          // FFFF = IE
        case 0x0F:
          return m_cpu->GetRegisters()->IE;
      }
      break;
    }
  }

  // CGB-only registers
  if (InCGBMode())
  {
    switch (index & 0xF0)
    {
      case 0x40:
      {
        switch (index & 0x0F)
        {
          case 0x0F: // FF4F - VBK - CGB Mode Only - VRAM Bank
            return m_vram_bank;

          case 0x0D: // FF4D - KEY1 - CGB Mode Only - Prepare Speed Switch
            return m_cgb_speed_switch;

          case 0x0C: // FF4C - Set by GBC boot rom
            return m_biosLatch ? m_reg_FF4C : 0xFF;
        }

        break;
      }

      case 0x50:
      {
        switch (index & 0x0F)
        {
          case 0x01: // FF51 - HDMA1 - CGB Mode Only - New DMA Source, High
          case 0x02: // FF52 - HDMA2 - CGB Mode Only - New DMA Source, Low
          case 0x03: // FF53 - HDMA3 - CGB Mode Only - New DMA Destination, High
          case 0x04: // FF54 - HDMA4 - CGB Mode Only - New DMA Destination, Low
          case 0x05: // FF55 - HDMA5 - CGB Mode Only - New DMA Length/Mode/Start
            m_display->Synchronize();
            return m_display->CPUReadRegister(index);
        }

        break;
      }

      case 0x60:
      {
        // LCD registers
        switch (index & 0x0F)
        {
          case 0x08: // FF68 - BCPS/BGPI - CGB Mode Only - Background Palette Index
          case 0x09: // FF69 - BCPD/BGPD - CGB Mode Only - Background Palette Data
          case 0x0A: // FF6A - OCPS/OBPI - CGB Mode Only - Sprite Palette Index
          case 0x0B: // FF6B - OCPD/OBPD - CGB Mode Only - Sprite Palette Data
            m_display->Synchronize();
            return m_display->CPUReadRegister(index);

          case 0x0C: // FF4C - Set by GBC boot rom
            return m_reg_FF6C;
        }

        break;
      }

      case 0x70:
      {
        switch (index & 0x0F)
        {
          case 0x00: // FF70 - SVBK - CGB Mode Only - WRAM Bank
            return (InCGBMode()) ? m_high_wram_bank : 0x00;
        }

        break;
      }
    }
  }

  // "high ram"
  if (index >= 0x80)
  {
    DebugAssert(index != 0xFF);
    return m_memory_zram[index - 0x80];
  }

  WARNING_LOG("Unhandled CPU IO register read: 0x{:02X}", index);
  // return 0x00;
  return m_memory_ioreg[index];
}

void System::CPUWriteIORegister(uint8 index, uint8 value)
{
  switch (index & 0xF0)
  {
    case 0x00:
    {
      switch (index & 0x0F)
      {
          // pad select
        case 0x00:
          m_pad_row_select = value & 0x30;
          return;

          // FF01 - SB serial data
        case 0x01:
          m_serial->Synchronize();
          m_serial->SetSerialData(value);
          return;

          // FF02 - SC serial control
        case 0x02:
          m_serial->Synchronize();
          m_serial->SetSerialControl(value);
          return;

          // FF04 - DIV - Divider Register (R/W)
        case 0x04:
          SynchronizeTimers();
          m_timer_divider = 0;
          return;

          // FF05 - TIMA - Timer counter (R/W)
        case 0x05:
          SynchronizeTimers();
          m_timer_counter = value;
          ScheduleTimerSynchronization();
          return;

          // FF06 - TMA - Timer Modulo (R/W)
        case 0x06:
          SynchronizeTimers();
          m_timer_overflow_value = value;
          ScheduleTimerSynchronization();
          return;

          // FF07 - TAC - Timer Control (R/W)
        case 0x07:
          SynchronizeTimers();
          m_timer_control = value;
          ScheduleTimerSynchronization();
          return;

          // interrupt flag
        case 0x0F:
          m_serial->Synchronize();
          m_display->Synchronize();
          SynchronizeTimers();
          m_cpu->GetRegisters()->IF = value;
          return;
      }

      break;
    }

    case 0x10:
    {
      switch (index & 0x0F)
      {
        case 0x00: // FF10 - NR10 - Channel 1 Sweep register (R/W)
        case 0x01: // FF11 - NR11 - Channel 1 Sound length/Wave pattern duty (R/W)
        case 0x02: // FF12 - NR12 - Channel 1 Volume Envelope (R/W)
        case 0x03: // FF13 - NR13 - Channel 1 Frequency lo (Write Only)
        case 0x04: // FF14 - NR14 - Channel 1 Frequency hi (R/W)
        case 0x05: // FF15 - NR?? - ???
        case 0x06: // FF16 - NR21 - Channel 2 Sound Length/Wave Pattern Duty (R/W)
        case 0x07: // FF17 - NR22 - Channel 2 Volume Envelope (R/W)
        case 0x08: // FF18 - NR23 - Channel 2 Frequency lo data (W)
        case 0x09: // FF19 - NR24 - Channel 2 Frequency hi data (R/W)
        case 0x0A: // FF1A - NR30 - Channel 3 Sound on/off (R/W)
        case 0x0B: // FF1B - NR31 - Channel 3 Sound Length
        case 0x0C: // FF1C - NR32 - Channel 3 Select output level (R/W)
        case 0x0D: // FF1D - NR33 - Channel 3 Frequency's lower data (W)
        case 0x0E: // FF1E - NR34 - Channel 3 Frequency's higher data (R/W)
        case 0x0F: // FF1F - NR?? - ???
          m_audio->Synchronize();
          m_audio->CPUWriteRegister(index, value);
          return;
      }

      break;
    }

    case 0x20:
    {
      switch (index & 0x0F)
      {
        case 0x00: // FF20 - NR41 - Channel 4 Sound Length (R/W)
        case 0x01: // FF21 - NR42 - Channel 4 Volume Envelope (R/W)
        case 0x02: // FF22 - NR43 - Channel 4 Polynomial Counter (R/W)
        case 0x03: // FF23 - NR44 - Channel 4 Counter/consecutive; Inital (R/W)
        case 0x04: // FF24 - NR50 - Channel control / ON-OFF / Volume (R/W)
        case 0x05: // FF25 - NR51 - Selection of Sound output terminal (R/W)
        case 0x06: // FF26 - NR52 - sound on/off
        case 0x07: // FF27 - ???
        case 0x08: // FF28 - ???
        case 0x09: // FF29 - ???
        case 0x0A: // FF2A - ???
        case 0x0B: // FF2B - ???
        case 0x0C: // FF2C - ???
        case 0x0D: // FF2D - ???
        case 0x0E: // FF2E - ???
        case 0x0F: // FF2F - ???
          m_audio->Synchronize();
          m_audio->CPUWriteRegister(index, value);
          return;
      }

      break;
    }

    case 0x30:
    {
      // FF30-FF3F - Wave Pattern RAM
      m_audio->Synchronize();
      m_audio->CPUWriteRegister(index, value);
      return;
    }

    case 0x40:
    {
      switch (index & 0x0F)
      {
        case 0x00: // FF40 - LCDC - LCD Control (R/W)
        case 0x01: // FF41 - STAT - LCDC Status (R/W)
        case 0x02: // FF42 - SCY - Scroll Y (R/W)
        case 0x03: // FF43 - SCX - Scroll X (R/W)
        case 0x04: // FF44 - LY - LCDC Y-Coordinate (R/W?)
        case 0x05: // FF45 - LYC - LY Compare(R / W)
        case 0x07: // FF47 - BGP - BG Palette Data (R/W) - Non CGB Mode Only
        case 0x08: // FF48 - OBP0 - Object Palette 0 Data (R/W) - Non CGB Mode Only
        case 0x09: // FF49 - OBP1 - Object Palette 1 Data(R / W) - Non CGB Mode Only
        case 0x0A: // FF4A - WY - Window Y Position (R/W)
        case 0x0B: // FF4B - WX - Window X Position minus 7 (R/W)
          m_display->Synchronize();
          m_display->CPUWriteRegister(index, value);
          return;

        case 0x0C: // FF4C - Set by GBC boot rom
        {
          if (m_biosLatch)
            m_reg_FF4C = value;

          return;
        }

        case 0x06: // FF46 - DMA - DMA Transfer and Start Address (W)
        {
          m_display->Synchronize();

          // Writing to this register launches a DMA transfer from ROM or RAM to OAM memory (sprite attribute table).
          // The written value specifies the transfer source address divided by 100h It takes 160 microseconds until the
          // transfer has completed (80 microseconds in CGB Double Speed Mode), during this time the CPU can access only
          // HRAM (memory at FF80-FFFE).
          uint16 source_address = (uint16)value * 256;
          OAMDMATransfer(source_address);
          return;
        }
      }

      break;
    }

    case 0x50:
    {
      switch (index & 0x0F)
      {
        case 0x00: // FF00 - BIOS enable/disable latch
          m_biosLatch = (value == 0);

          // 0x4C is set to 0x04 for CGB-in-DMG mode, 0xC0 otherwise.
          if (m_boot_mode == SYSTEM_MODE_CGB)
            m_current_mode = (m_reg_FF4C == 0x04) ? SYSTEM_MODE_DMG : SYSTEM_MODE_CGB;
          return;
      }

      break;
    }

    case 0xF0:
    {
      switch (index & 0x0F)
      {
        case 0x0F:
          // F0-FE is high ram below, FF = interrupt flag
          m_cpu->GetRegisters()->IE = value;
          return;
      }

      break;
    }
  }

  // CGB-only registers
  if (InCGBMode())
  {
    switch (index & 0xF0)
    {
      case 0x40:
      {
        switch (index & 0x0F)
        {
          case 0x0F: // FF4F - VBK - CGB Mode Only - VRAM Bank
            m_vram_bank = value & 0x1;
            return;

          case 0x0D: // FF4D - KEY1 - CGB Mode Only - Prepare Speed Switch
            m_cgb_speed_switch = (m_cgb_speed_switch & 0xFE) | (value & 0x01);
            return;
        }

        break;
      }

      case 0x50:
      {
        switch (index & 0x0F)
        {
          case 0x01: // FF51 - HDMA1 - CGB Mode Only - New DMA Source, High
          case 0x02: // FF52 - HDMA2 - CGB Mode Only - New DMA Source, Low
          case 0x03: // FF53 - HDMA3 - CGB Mode Only - New DMA Destination, High
          case 0x04: // FF54 - HDMA4 - CGB Mode Only - New DMA Destination, Low
          case 0x05: // FF55 - HDMA5 - CGB Mode Only - New DMA Length/Mode/Start
            m_display->Synchronize();
            m_display->CPUWriteRegister(index, value);
            return;
        }

        break;
      }

      case 0x60:
      {
        switch (index & 0x0F)
        {
          case 0x08: // FF68 - BCPS/BGPI - CGB Mode Only - Background Palette Index
          case 0x09: // FF69 - BCPD/BGPD - CGB Mode Only - Background Palette Data
          case 0x0A: // FF6A - OCPS/OBPI - CGB Mode Only - Sprite Palette Index
          case 0x0B: // FF6B - OCPD/OBPD - CGB Mode Only - Sprite Palette Data
            m_display->Synchronize();
            m_display->CPUWriteRegister(index, value);
            return;

          case 0x0C: // FF6C - Set by GBC boot rom
            m_reg_FF6C = value & 0x01;
            return;
        }

        break;
      }

      case 0x70:
      {
        switch (index & 0x0F)
        {
          case 0x00: // FF70 - SVBK - CGB Mode Only - WRAM Bank
            m_high_wram_bank = value & 0x07;

            // Writing a value of 01h-07h will select Bank 1-7, writing a value of 00h will select Bank 1 either.
            if (m_high_wram_bank == 0)
              m_high_wram_bank = 1;

            return;
        }

        break;
      }
    }
  }

  // "high ram"
  if (index >= 0x80)
  {
    DebugAssert(index != 0xFF);
    m_memory_zram[index - 0x80] = value;
    return;
  }

  WARNING_LOG("Unhandled CPU IO register write: 0x{:02X} (value 0x{:02X})", index, value);
  m_memory_ioreg[index] = value;
}

void System::CPUInterruptRequest(uint8 index)
{
  TRACE_LOG("CPU raise interrupt {}", index);
  m_cpu->RaiseInterrupt(index);
}

#define SYSTEM static_cast<System*>(userdata)

static void R2SessionSetInputValue(void* userdata, uint32_t session_index, RETRO2_INPUT_DEVICE_TYPE device_type,
                                   uint32_t input_id, float value, void* session_userdata)
{
  if (session_index != 0)
    return;

  const bool down = (value > 0.5f);
  switch (input_id)
  {
    case RETRO2_GAMEPAD_INPUT_ID_DPAD_UP:
      SYSTEM->SetPadDirection(PAD_DIRECTION_UP, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_DPAD_DOWN:
      SYSTEM->SetPadDirection(PAD_DIRECTION_DOWN, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_DPAD_LEFT:
      SYSTEM->SetPadDirection(PAD_DIRECTION_LEFT, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_DPAD_RIGHT:
      SYSTEM->SetPadDirection(PAD_DIRECTION_RIGHT, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_SOUTH:
      SYSTEM->SetPadButton(PAD_BUTTON_B, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_EAST:
      SYSTEM->SetPadButton(PAD_BUTTON_A, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_START:
      SYSTEM->SetPadButton(PAD_BUTTON_START, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_BACK:
      SYSTEM->SetPadButton(PAD_BUTTON_SELECT, down);
      break;

    case RETRO2_GAMEPAD_INPUT_ID_LEFT_STICK_X:
    {
      // need to check last stick
    }
    break;

    case RETRO2_GAMEPAD_INPUT_ID_LEFT_STICK_Y:
    {
      // need to check last stick
    }
    break;

    default:
      break;
  }
}

static void R2SessionRestart(void* userdata)
{
  SYSTEM->Reset();
}

static void R2SessionShutdown(void* userdata)
{
  delete SYSTEM;
}

static void R2SessionRunFrame(void* userdata)
{
  SYSTEM->ExecuteFrame();
}

void System::SetMemoryMap()
{
  static constexpr u32 NOMASK = ~0u;
  const Retro2MemoryMapEntry entries[] = {
    {nullptr, 0x0000, 0x8000, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_ROM, RETRO2_MEMORY_MAP_FLAG_READ_ONLY,
     static_cast<u32>(m_cartridge->GetROM().size()), const_cast<u8*>(m_cartridge->GetROM().data())},
    {nullptr, 0x8000, 0x9800, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_RAM, RETRO2_MEMORY_MAP_FLAG_NONE,
     sizeof(m_memory_vram), &m_memory_vram[0][0]},
    {nullptr, 0xA000, 0xC000, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_SAVE_MEMORY, RETRO2_MEMORY_MAP_FLAG_NONE,
     m_cartridge->GetExternalRAMSize(), m_cartridge->GetExternalRAM()},
    {nullptr, 0xC000, 0xE000, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_RAM, RETRO2_MEMORY_MAP_FLAG_NONE, 0x2000,
     &m_memory_wram[0][0]},
    {nullptr, 0xE000, 0xFE00, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_RAM, RETRO2_MEMORY_MAP_FLAG_NONE, 0x1E00,
     &m_memory_wram[0][0]},
    {nullptr, 0xFE00, 0xFF00, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_RAM, RETRO2_MEMORY_MAP_FLAG_NONE,
     sizeof(m_memory_oam), m_memory_oam},
    // I/O registers ommitted
    {nullptr, 0xFF80, 0xFFFE, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_RAM, RETRO2_MEMORY_MAP_FLAG_NONE,
     sizeof(m_memory_zram), m_memory_zram},
    {nullptr, 0x10000, 0x16000, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_RAM, RETRO2_MEMORY_MAP_FLAG_NONE,
     sizeof(m_memory_wram) - (sizeof(m_memory_wram[0]) * 2), &m_memory_wram[2][0]},
    {nullptr, 0x16000, 0x34000, 0, 0, NOMASK, NOMASK, RETRO2_MEMORY_MAP_TYPE_SAVE_MEMORY, RETRO2_MEMORY_MAP_FLAG_NONE,
     (m_cartridge->GetExternalRAMSize() > 0x2000) ? (m_cartridge->GetExternalRAMSize() - 0x2000) : 0,
     m_cartridge->GetExternalRAM() + 0x2000},
  };

  R2Session.SetMemoryMap(entries, std::size(entries));
}

static size_t R2SessionGetSaveStateBufferSize(void*)
{
  return 1 * 1024 * 1024;
}

static bool R2SessionLoadStateFromBuffer(void* userdata, const uint8_t* buffer, size_t buffer_size,
                                         uint32_t data_version, Retro2Error* error)
{
  Error merror;
  if (SYSTEM->LoadState(std::span<const u8>(buffer, buffer_size), &merror))
    return true;

  R2Error.SetErrorString(error, merror.GetDescription().c_str());
  return false;
}

static bool R2SessionSaveStateToBuffer(void* userdata, uint8_t* buffer, size_t buffer_size, size_t* written_size,
                                       uint32_t* data_version, Retro2Error* error)
{
  Error merror;
  if (SYSTEM->SaveState(std::span<u8>(buffer, buffer_size), written_size, &merror))
  {
    *data_version = 1;
    return true;
  }

  R2Error.SetErrorString(error, merror.GetDescription().c_str());
  return false;
}

static void R2SessionSettingsChanged(void* userdata)
{
  SYSTEM->UpdateSettings();
}

static RETRO2_SESSION_START_RESULT R2SessionProviderStartSessionFromMemory(const Retro2StartSessionParams* params, const uint8_t* memory,
                                                    size_t memory_size, PFN_Retro2SetPluginSessionInterface set_session,
                                                    Retro2Error* error)
{
  Error merror;
  std::unique_ptr<Cartridge> cart = std::make_unique<Cartridge>(std::span<const u8>(memory, memory_size));
  if (!cart->Load(&merror))
  {
    R2Error.SetErrorString(error, merror.GetDescription().c_str());
    return RETRO2_SESSION_START_RESULT_UNSUPPORTED_CONTENT;
  }

  std::unique_ptr<System> system = std::make_unique<System>();
  const SYSTEM_MODE system_mode = cart->GetSystemMode();
  if (!system->Init(system_mode, nullptr, 0, std::move(cart), &merror))
  {
    R2Error.SetErrorString(error, merror.GetDescription().c_str());
    return RETRO2_SESSION_START_RESULT_FAILURE;
  }

  if (!R2Session.GetVideoInterface(RETRO2_VIDEO_INTERFACE_FRAMEBUFFER, RETRO2_FRAMEBUFFER_VIDEO_INTERFACE_VERSION,
                                   nullptr, &R2Video, error))
  {
    return RETRO2_SESSION_START_RESULT_MISSING_INTERFACE;
  }

  IRetro2PluginSession plugin_session = {};
  plugin_session.Shutdown = R2SessionShutdown;
  plugin_session.Restart = R2SessionRestart;
  plugin_session.SettingsChanged = R2SessionSettingsChanged;
  plugin_session.SetInputValue = R2SessionSetInputValue;
  plugin_session.RunFrame = R2SessionRunFrame;
  plugin_session.GetSaveStateBufferSize = R2SessionGetSaveStateBufferSize;
  plugin_session.LoadStateFromBuffer = R2SessionLoadStateFromBuffer;
  plugin_session.SaveStateToBuffer = R2SessionSaveStateToBuffer;

  const char* system_type_name = (system_mode == SYSTEM_MODE_CGB) ? "gbc" : "gb";
  if (!set_session(&plugin_session, RETRO2_PLUGIN_SESSION_INTERFACE_VERSION, system.get(), system_type_name,
                   RETRO2_SESSION_RUN_MODE_FRAME, error))
  {
    return RETRO2_SESSION_START_RESULT_MISSING_INTERFACE;
  }

  R2Session.SetInputDevice(0, RETRO2_INPUT_DEVICE_TYPE_GAMEPAD, RETRO2_GAMEPAD_INPUT_FLAG_NO_ANALOGS, nullptr);
  R2Session.SetVideoFrameRate(59.727500569606);
  R2Session.SetVideoDisplayAspectRatio(1.0f);

  if (system->GetCartridge()->GetExternalRAMSize() > 0)
  {
    R2Session.SetSaveMemory(system->GetCartridge()->GetExternalRAM(), system->GetCartridge()->GetExternalRAMSize(),
                            true, 60);
  }

  system->SetMemoryMap();
  system.release();
  return RETRO2_SESSION_START_RESULT_SUCCESS;
}

bool Retro2GetSessionProvider(IRetro2SessionProvider* prov, Retro2Error* error)
{
  static constexpr IRetro2SessionProvider our_prov = {
    .Initialize = nullptr,
    .Shutdown = nullptr,
    .StartSessionFromPath = nullptr,
    .StartSessionFromMemory = R2SessionProviderStartSessionFromMemory,
    .StartSessionWithoutContent = nullptr,
  };

  std::memcpy(prov, &our_prov, sizeof(our_prov));
  return true;
}

bool Retro2PluginInitialize(const Retro2PluginInitializeParams* params, Retro2PluginInitializeInfo* info,
                            Retro2Error* error)
{
  R2Error = *params->IError;

  const IRetro2Log* ilog =
    static_cast<const IRetro2Log*>(params->QueryInterface(RETRO2_INTERFACE_LOG, RETRO2_LOG_INTERFACE_VERSION, error));
  if (!ilog)
  {
    R2Error.SetErrorString(error, "Log interface is required.");
    return false;
  }

  static constexpr const Retro2InterfaceLoadEntry s_load_entries[] = {
    RETRO2_INTERFACE_LOAD_ENTRY(R2Session, SESSION),
    RETRO2_INTERFACE_LOAD_ENTRY(R2Settings, SETTINGS),
  };
  if (!Retro2LoadInterfaces(params, s_load_entries, std::size(s_load_entries), error))
    return false;

  Log::Initialize(ilog);

  info->PluginName = "r2gbe";
  info->MinimumSaveStateVersion = 1;
  info->MaximumSaveStateVersion = 1;

  return true;
}

void Retro2PluginShutdown()
{
}
