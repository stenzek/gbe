LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ROOT_PATH := $(LOCAL_PATH)/../../../../..
INCLUDE_DIRS := -I$(ROOT_PATH)/dep/YBaseLib/Include -I$(ROOT_PATH)/dep/Gb_Snd_Emu -I$(ROOT_PATH)/src

YBASELIB_SRC_BASE := $(ROOT_PATH)/dep/YBaseLib/Source
YBASELIB_SRC_FILES := \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidBarrier.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidConditionVariable.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidEvent.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidFileSystem.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidPlatform.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidReadWriteLock.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Android/AndroidThread.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Assert.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Atomic.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/BinaryBlob.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/BinaryReadBuffer.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/BinaryReader.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/BinaryWriteBuffer.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/BinaryWriter.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/ByteStream.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/CallbackQueue.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/CircularBuffer.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/CPUID.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/CRC32.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/CString.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Endian.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Error.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Exception.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/FileSystem.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/HashTrait.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Log.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Math.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/MD5Digest.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Memory.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/NameTable.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/NumericLimits.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/ProgressCallbacks.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/ReferenceCounted.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Sockets/Generic/BufferedStreamSocket.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Sockets/Generic/ListenSocket.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Sockets/Generic/SocketMultiplexer.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Sockets/Generic/StreamSocket.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Sockets/SocketAddress.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/StringConverter.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/String.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/StringParser.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/TaskQueue.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/TextReader.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/TextWriter.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/ThreadPool.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Timer.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/Timestamp.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/XMLReader.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/XMLWriter.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/ZipArchive.cpp \
    $(YBASELIB_SRC_BASE)/YBaseLib/ZLibHelpers.cpp

GBSNDEMU_SRC_BASE := $(ROOT_PATH)/dep/Gb_Snd_Emu
GBSNDEMU_SRC_FILES := \
    $(GBSNDEMU_SRC_BASE)/Blip_Buffer.cpp \
    $(GBSNDEMU_SRC_BASE)/Effects_Buffer.cpp \
    $(GBSNDEMU_SRC_BASE)/Gb_Apu.cpp \
    $(GBSNDEMU_SRC_BASE)/Gb_Apu_State.cpp \
    $(GBSNDEMU_SRC_BASE)/Gb_Oscs.cpp \
    $(GBSNDEMU_SRC_BASE)/Multi_Buffer.cpp

GBE_SRC_BASE := $(ROOT_PATH)/src
GBE_SRC_FILES := \
    $(GBE_SRC_BASE)/audio.cpp \
    $(GBE_SRC_BASE)/cartridge.cpp \
    $(GBE_SRC_BASE)/cpu.cpp \
    $(GBE_SRC_BASE)/cpu_disasm.cpp \
    $(GBE_SRC_BASE)/cpu_instruction_table.cpp \
    $(GBE_SRC_BASE)/display.cpp \
    $(GBE_SRC_BASE)/link.cpp \
    $(GBE_SRC_BASE)/serial.cpp \
    $(GBE_SRC_BASE)/structures.cpp \
    $(GBE_SRC_BASE)/system.cpp \
    $(GBE_SRC_BASE)/system_link.cpp

JNI_SRC_FILES := \
	GBSystem_jni.cpp

ALL_SRC_FILES := $(JNI_SRC_FILES) $(GBE_SRC_FILES) $(GBSNDEMU_SRC_FILES) $(YBASELIB_SRC_FILES)

LOCAL_MODULE    := gbe
LOCAL_CFLAGS	:= -std=c++11 $(INCLUDE_DIRS)
LOCAL_SRC_FILES := $(ALL_SRC_FILES:$(LOCAL_PATH)/%=%)
LOCAL_LDLIBS 	:= -llog -ljnigraphics

include $(BUILD_SHARED_LIBRARY)


