#pragma once
#include "structures.h"
#include "retro2/retro2_session.h"

class Error;

class System;
class StateWrapper;
class Gb_Apu;
class Stereo_Buffer;

class Audio
{
  friend System;

public:
  Audio(System* system);
  ~Audio();

  void Reset();
  void Synchronize();

  // register access
  uint8 CPUReadRegister(uint8 index) const;
  void CPUWriteRegister(uint8 index, uint8 value);

private:
  // state saving
  bool DoState(StateWrapper& sw);

  System* m_system;

  Gb_Apu* m_apu;
  Stereo_Buffer* m_buffer;

  uint32 m_last_cycle;
  uint32 m_cycles_since_frame;

  Retro2AudioStream m_stream = nullptr;
};
