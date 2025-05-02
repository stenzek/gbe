add_library(Gb_Snd_Emu STATIC
  blargg_common.h
  blargg_config.h
  blargg_source.h
  Blip_Buffer.cpp
  Blip_Buffer.h
  Gb_Apu.cpp
  Gb_Apu.h
  Gb_Apu_State.cpp
  Gb_Oscs.cpp
  Gb_Oscs.h
  Multi_Buffer.cpp
  Multi_Buffer.h
)

target_include_directories(Gb_Snd_Emu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

