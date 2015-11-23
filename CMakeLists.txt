cmake_minimum_required(VERSION 2.8)

# handle clang
if("${CMAKE_CXX_COMPILER}" MATCHES "clang(\\+\\+)?$" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR APPLE)
    set(CMAKE_COMPILER_IS_CLANGXX 1)
endif()

# pull in modules
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules/")

# set project
project(gbe CXX C)

# determine 32bit vs 64bit build
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BITNESS 64)
else()
    set(BITNESS 32)
endif()

# find system dependancies
if(EMSCRIPTEN)
    # emscripten libraries
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_SDL=2 -s USE_ZLIB=1")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL=2 -s USE_ZLIB=1")
else()
    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)
    find_package(SDL2 REQUIRED)
endif()

# set up install directory
if(BITNESS EQUAL 64)
    if(CMAKE_BUILD_TYPE EQUAL "Debug")
        set(INSTALL_BINARIES_DIRECTORY "bin64-debug")
    else()
        set(INSTALL_BINARIES_DIRECTORY "bin64")
    endif()
else()
    if(CMAKE_BUILD_TYPE EQUAL "Debug")
        set(INSTALL_BINARIES_DIRECTORY "bin32-debug")
    else()
        set(INSTALL_BINARIES_DIRECTORY "bin32")
    endif()
endif()    

# enable c99/ c++11
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANGXX OR EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

# set cflags that are common to dependancies
add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-D_GNU_SOURCE)

# update cflags for debug build
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-D_DEBUG)
    message("Building debug executable.")
endif()

# build options
if(EMSCRIPTEN OR ANDROID)
    set(HAVE_ZLIB "1")
else()
    set(HAVE_ZLIB "1")
endif()

# kill annoying clang warnings
if(CMAKE_COMPILER_IS_CLANGXX)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Qunused-arguments")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments")
endif()

# same for clang/emscripten
if(EMSCRIPTEN)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-warn-absolute-paths")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-warn-absolute-paths")
endif()


# set up common variables
set(ENGINE_BASE_DIRECTORY "${CMAKE_SOURCE_DIR}/Engine/Source")

# set up cflags
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANGXX OR EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic")

    # kill annoying warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")
endif()

# pull in include directories
include_directories("${CMAKE_BINARY_DIR}")

# generate config.h
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/config.h)
add_definitions(-DHAVE_CONFIG_H)

###################### YBaseLib ############################

set(YBASELIB_SRC_BASE ${CMAKE_SOURCE_DIR}/dep/YBaseLib/Source)
set(YBASELIB_INCLUDES ${CMAKE_SOURCE_DIR}/dep/YBaseLib/Include)
set(YBASELIB_SRC_FILES
    ${YBASELIB_SRC_BASE}/YBaseLib/Android
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidBarrier.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidConditionVariable.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidEvent.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidFileSystem.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidPlatform.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidReadWriteLock.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Android/AndroidThread.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Assert.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Atomic.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/BinaryBlob.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/BinaryReadBuffer.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/BinaryReader.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/BinaryWriteBuffer.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/BinaryWriter.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/ByteStream.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/CallbackQueue.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/CircularBuffer.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/CPUID.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/CRC32.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/CString.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Endian.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Error.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Exception.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/FileSystem.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HashTrait.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5/HTML5Barrier.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5/HTML5ConditionVariable.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5/HTML5FileSystem.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5/HTML5Platform.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5/HTML5ReadWriteLock.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/HTML5/HTML5Thread.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Log.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Math.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/MD5Digest.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Memory.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/NameTable.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/NumericLimits.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXBarrier.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXConditionVariable.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXFileSystem.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXPlatform.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXReadWriteLock.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXSubprocess.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/POSIX/POSIXThread.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/ProgressCallbacks.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/ReferenceCounted.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets/Generic
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets/Generic/BufferedStreamSocket.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets/Generic/ListenSocket.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets/Generic/SocketMultiplexer.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets/Generic/StreamSocket.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Sockets/SocketAddress.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/StringConverter.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/String.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/StringParser.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/TaskQueue.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/TextReader.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/TextWriter.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/ThreadPool.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Timer.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Timestamp.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsBarrier.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsConditionVariable.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsFileSystem.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsPlatform.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsReadWriteLock.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsSubprocess.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/Windows/WindowsThread.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/XMLReader.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/XMLWriter.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/ZipArchive.cpp
    ${YBASELIB_SRC_BASE}/YBaseLib/ZLibHelpers.cpp
)

add_library(YBaseLib STATIC ${YBASELIB_SRC_FILES})
target_include_directories(YBaseLib PRIVATE ${YBASELIB_INCLUDES} ${YBASELIB_SRC_BASE} ${LIBXML2_INCLUDE_DIR} ${ZLIB_INCLUDE_DIRS})
target_include_directories(YBaseLib PUBLIC ${YBASELIB_INCLUDES})
target_link_libraries(YBaseLib ${ZLIB_LIBRARIES} ${LIBXML2_LIBRARIES})

###################### GBSNDEMU ############################

set(GBSNDEMU_SRC_BASE ${CMAKE_SOURCE_DIR}/dep/Gb_Snd_Emu)
set(GBSNDEMU_INCLUDES ${CMAKE_SOURCE_DIR}/dep/Gb_Snd_Emu)
set(GBSNDEMU_SRC_FILES
    ${GBSNDEMU_SRC_BASE}/Blip_Buffer.cpp
    ${GBSNDEMU_SRC_BASE}/Effects_Buffer.cpp
    ${GBSNDEMU_SRC_BASE}/Gb_Apu.cpp
    ${GBSNDEMU_SRC_BASE}/Gb_Apu_State.cpp
    ${GBSNDEMU_SRC_BASE}/Gb_Oscs.cpp
    ${GBSNDEMU_SRC_BASE}/Multi_Buffer.cpp
)

add_library(GbSndEmu STATIC ${GBSNDEMU_SRC_FILES})
target_include_directories(GbSndEmu PRIVATE ${GBSNDEMU_INCLUDES} ${GBSNDEMU_SRC_BASE})
target_include_directories(GbSndEmu PUBLIC ${GBSNDEMU_INCLUDES})

###################### gbe ############################

set(GBE_SRC_BASE ${CMAKE_SOURCE_DIR}/src)
set(GBE_INCLUDES ${CMAKE_SOURCE_DIR}/src)
set(GBE_SRC_FILES
    ${GBE_SRC_BASE}/audio.cpp
    ${GBE_SRC_BASE}/cartridge.cpp
    ${GBE_SRC_BASE}/cpu.cpp
    ${GBE_SRC_BASE}/cpu_disasm.cpp
    ${GBE_SRC_BASE}/display.cpp
    ${GBE_SRC_BASE}/link.cpp
    ${GBE_SRC_BASE}/main.cpp
    ${GBE_SRC_BASE}/serial.cpp
    ${GBE_SRC_BASE}/structures.cpp
    ${GBE_SRC_BASE}/system.cpp
)

add_executable(gbe ${GBE_SRC_FILES})
target_include_directories(gbe PRIVATE ${GBE_INCLUDES} ${GBE_SRC_BASE} ${SDL2_INCLUDES})
target_include_directories(gbe PUBLIC ${GBE_INCLUDES} ${SDL2_INCLUDE_DIR})
target_link_libraries(gbe GbSndEmu YBaseLib ${SDL2_LIBRARY})


