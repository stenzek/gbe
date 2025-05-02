add_library(common
  assert.cpp
  assert.h
  crc32.cpp
  crc32.h
  error.cpp
  error.h
  heap_array.h
  log_channels.h
  log.cpp
  log.h
  string_util.cpp
  string_util.h
  timer.cpp
  timer.h
  types.h
  windows_headers.h
)

target_include_directories(common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(common PUBLIC fmt Threads::Threads fast_float retro2)

