#include "audio.h"
#include "state_wrapper.h"
#include "system.h"

#include "Gb_Snd_Emu/Gb_Apu.h"
#include "Gb_Snd_Emu/Multi_Buffer.h"

#include "common/log.h"

#include <cstring>

LOG_CHANNEL(Audio);

static const uint32 OUTPUT_BUFFER_SIZE = 11025 * 2; // 0.25 seconds output buffering (stereo)
static const uint32 PUSH_FREQUENCY_IN_CYCLES = 8192;

Audio::Audio(System* system)
  : m_system(system), m_buffer(new Stereo_Buffer()), m_apu(new Gb_Apu()), m_last_cycle(0), m_cycles_since_frame(0)
{
  m_buffer->clock_rate(4194304);
  m_buffer->set_sample_rate(44100);
  m_apu->set_output(m_buffer->center(), m_buffer->left(), m_buffer->right());

  m_stream = R2Session.CreateAudioStream(44100, 2, RETRO2_AUDIO_FORMAT_S16, true, nullptr);
}

Audio::~Audio()
{
  R2Session.DestroyAudioStream(m_stream);

  delete m_apu;
  delete m_buffer;
}

void Audio::Reset()
{
  m_apu->reset((m_system->InCGBMode()) ? Gb_Apu::mode_cgb : Gb_Apu::mode_dmg, false);
  m_last_cycle = 0;
  m_cycles_since_frame = 0;
}

bool Audio::DoState(StateWrapper& sw)
{
  sw.Do(&m_last_cycle);
  sw.Do(&m_cycles_since_frame);

  gb_apu_state_t state;

  if (sw.IsReading())
  {
    sw.DoBytes(&state, sizeof(state));
    if (sw.HasError())
      return false;

    m_apu->reset((m_system->InCGBMode()) ? Gb_Apu::mode_cgb : Gb_Apu::mode_dmg, false);
    const char* err = m_apu->load_state(state);
    if (err)
    {
      ERROR_LOG("Failed to load APU state: {}", err);
      return false;
    }

    m_buffer->end_frame(PUSH_FREQUENCY_IN_CYCLES);
    m_buffer->clear();
  }
  else
  {
    m_apu->save_state(&state);
    sw.DoBytes(&state, sizeof(state));
  }

  return !sw.HasError();
}

void Audio::Synchronize()
{
  uint32 cycles_to_execute = m_system->CalculateCycleCount(m_last_cycle);
  m_last_cycle = m_system->GetCycleNumber();
  m_cycles_since_frame += cycles_to_execute;

  while (m_cycles_since_frame >= PUSH_FREQUENCY_IN_CYCLES)
  {
    m_cycles_since_frame -= PUSH_FREQUENCY_IN_CYCLES;

    // push a frame
    m_apu->end_frame(PUSH_FREQUENCY_IN_CYCLES);

    // copy to output buffer
    m_buffer->end_frame(PUSH_FREQUENCY_IN_CYCLES);

    // gather samples
    size_t remaining = m_buffer->samples_avail() / 2;
    while (remaining > 0)
    {
      int16_t* samples;
      size_t max_frames;
      R2Session.BeginWriteAudioFrames(m_stream, reinterpret_cast<void**>(&samples), &max_frames);

      size_t to_copy = std::min(remaining, max_frames);
      if (to_copy > 0)
        m_buffer->read_samples(samples, static_cast<long>(to_copy * 2));

      remaining -= to_copy;
      R2Session.EndWriteAudioFrames(m_stream, to_copy);
    }
  }

  m_system->SetNextAudioSyncCycle(PUSH_FREQUENCY_IN_CYCLES - m_cycles_since_frame);
}

uint8 Audio::CPUReadRegister(uint8 index) const
{
  uint32 op_time = m_cycles_since_frame + m_system->CalculateCycleCount(m_last_cycle);
  return (uint8)m_apu->read_register(op_time, 0xFF00 | index);
}

void Audio::CPUWriteRegister(uint8 index, uint8 value)
{
  uint32 op_time = m_cycles_since_frame + m_system->CalculateCycleCount(m_last_cycle);
  return m_apu->write_register(op_time, 0xFF00 | index, value);
}
