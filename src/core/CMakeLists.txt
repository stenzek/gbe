add_retro2_plugin(core LIBNAME gbe SOURCES
  audio.cpp
  audio.h
  cartridge.cpp
  cartridge.h
  cpu.cpp
  cpu.h
  cpu_disasm.cpp
  display.cpp
  display.h
  link.cpp
  link.h
  serial.cpp
  serial.h
  state_wrapper.cpp
  state_wrapper.h
  structures.h
  system.cpp
  system.h
)

target_include_directories(core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(core PRIVATE fmt common Gb_Snd_Emu scmversion)

# Copy in resources.
file(GLOB_RECURSE RESOURCE_FILES ${CMAKE_SOURCE_DIR}/data/*)
foreach(path IN LISTS RESOURCE_FILES)
  get_filename_component(file ${path} NAME)
  if("${file}" MATCHES "^\\.") # Don't copy macOS garbage (mainly Finder's .DS_Store files) into application
    continue()
  endif()
  get_filename_component(dir ${path} DIRECTORY)
  file(RELATIVE_PATH subdir "${CMAKE_SOURCE_DIR}/data/" ${dir})
  add_custom_command(TARGET core POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:core>/${subdir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${path}" "$<TARGET_FILE_DIR:core>/${subdir}")
endforeach()

