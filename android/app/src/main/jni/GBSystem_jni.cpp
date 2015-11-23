#include "system.h"
#include "cartridge.h"
#include "display.h"
#include "audio.h"
#include "link.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Error.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include "YBaseLib/CString.h"
#include "YBaseLib/Thread.h"
#include "YBaseLib/Math.h"
#include "YBaseLib/Platform.h"
#include "YBaseLib/BinaryReader.h"
#include "YBaseLib/BinaryWriter.h"
#include <jni.h>
#include <android/bitmap.h>
#include <GLES2/gl2.h>
Log_SetChannel(GBESystemNative);

// method ids for callbacks
static JavaVM *jvm;
static jclass GBSystem_Class;
static jfieldID GBSystem_Field_NativePointer;
static jmethodID GBSystem_Method_OnScreenBufferReady;
static jmethodID GBSystem_Method_LoadCartridgeRAM;
static jmethodID GBSystem_Method_SaveCartridgeRAM;
static jmethodID GBSystem_Method_LoadCartridgeRTC;
static jmethodID GBSystem_Method_SaveCartridgeRTC;

// helper for retreiving the current per-thread jni environment
static JNIEnv *GetJNIEnv()
{
    JNIEnv *env;
    if (jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return nullptr;
    else
        return env;
}

// native bridge class
class GBSystemNative : public System::CallbackInterface
{
public:
    GBSystemNative(jobject jobj)
        : m_jobject(jobj)
        , m_cart(nullptr)
        , m_system(new System(this))
    {
        Y_memzero(m_framebuffer, sizeof(m_framebuffer));
    }

    ~GBSystemNative()
    {
        delete m_cart;
        delete m_system;
    }

    jobject GetJObject()
    {
        return m_jobject;
    }

    System *GetSystem()
    {
        return m_system;
    }

    Cartridge *GetCartridge()
    {
        return m_cart;
    }

    bool LoadCartridge(const byte *cart, uint32 cart_length, Error *error)
    {
        delete m_cart;
        m_cart = new Cartridge(m_system);

        ByteStream *pStream = ByteStream_CreateReadOnlyMemoryStream(cart, cart_length);
        if (!m_cart->Load(pStream, error))
        {
            pStream->Release();
            delete m_cart;
            m_cart = nullptr;
            return false;
        }

        pStream->Release();
        return true;
    }

    virtual void PresentDisplayBuffer(const void *pPixels, uint32 row_stride) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return;

        DebugAssert(row_stride == Display::SCREEN_WIDTH * 4);

        env->MonitorEnter(m_jobject);
        {
            Y_memcpy(m_framebuffer, pPixels, sizeof(m_framebuffer));
            env->CallVoidMethod(m_jobject, GBSystem_Method_OnScreenBufferReady);
        }
        env->MonitorExit(m_jobject);
    }

    virtual bool LoadCartridgeRAM(void *pData, size_t expected_data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return false;

        jbyteArray dataArray = env->NewByteArray(expected_data_size);
        jboolean result = env->CallBooleanMethod(m_jobject, GBSystem_Method_LoadCartridgeRAM, dataArray, (int)expected_data_size);
        if (result)
        {
            jbyte *localData = env->GetByteArrayElements(dataArray, nullptr);
            Y_memcpy(pData, localData, expected_data_size);
            env->ReleaseByteArrayElements(dataArray, localData, JNI_ABORT);
        }

        return result;
    }

    virtual void SaveCartridgeRAM(const void *pData, size_t data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return;

        jbyteArray dataArray = env->NewByteArray(data_size);
        jbyte *localDataArray = env->GetByteArrayElements(dataArray, nullptr);
        Y_memcpy(localDataArray, pData, data_size);
        env->ReleaseByteArrayElements(dataArray, localDataArray, JNI_COMMIT);

        env->CallVoidMethod(m_jobject, GBSystem_Method_SaveCartridgeRAM, dataArray, (int)data_size);
    }

    virtual bool LoadCartridgeRTC(void *pData, size_t expected_data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return false;

        jbyteArray dataArray = env->NewByteArray(expected_data_size);
        jboolean result = env->CallBooleanMethod(m_jobject, GBSystem_Method_LoadCartridgeRTC, dataArray, (int)expected_data_size);
        if (result)
        {
            jbyte *localData = env->GetByteArrayElements(dataArray, nullptr);
            Y_memcpy(pData, localData, expected_data_size);
            env->ReleaseByteArrayElements(dataArray, localData, JNI_ABORT);
        }

        return result;
    }

    virtual void SaveCartridgeRTC(const void *pData, size_t data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return;

        jbyteArray dataArray = env->NewByteArray(data_size);
        jbyte *localDataArray = env->GetByteArrayElements(dataArray, nullptr);
        Y_memcpy(localDataArray, pData, data_size);
        env->ReleaseByteArrayElements(dataArray, localDataArray, JNI_COMMIT);

        env->CallVoidMethod(m_jobject, GBSystem_Method_SaveCartridgeRTC, dataArray, (int)data_size);
    }

    void CopyFrameBufferToBitmap(JNIEnv *env, jobject destinationBitmap)
    {
        int res;

        AndroidBitmapInfo info;
        res = AndroidBitmap_getInfo(env, destinationBitmap, &info);
        Assert(res == 0);
        Assert(info.format == ANDROID_BITMAP_FORMAT_RGBA_8888);
        Assert(info.width == Display::SCREEN_WIDTH && info.height == Display::SCREEN_HEIGHT);

        void *bitmapPixels;
        res = AndroidBitmap_lockPixels(env, destinationBitmap, &bitmapPixels);
        Assert(res == 0);

        uint32 ourStride = Display::SCREEN_WIDTH * 4;
        if (info.stride == ourStride)
            Y_memcpy(bitmapPixels, m_framebuffer, sizeof(m_framebuffer));
        else
            Y_memcpy_stride(bitmapPixels, info.stride, m_framebuffer, ourStride, sizeof(uint32) * Display::SCREEN_WIDTH, Display::SCREEN_HEIGHT);

        res = AndroidBitmap_unlockPixels(env, destinationBitmap);
        Assert(res == 0);
    }

    void CopyFrameBufferToGLTexture(int glTextureId)
    {
        glBindTexture(GL_TEXTURE_2D, glTextureId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Display::SCREEN_WIDTH, Display::SCREEN_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, m_framebuffer);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

private:
    jobject m_jobject;
    Cartridge *m_cart;
    System *m_system;
    byte m_framebuffer[Display::SCREEN_WIDTH * Display::SCREEN_HEIGHT * 4];
};

static void ThrowGBSystemException(JNIEnv *env, const char *format, ...)
{
    SmallString message;
    va_list ap;
    va_start(ap, format);
    message.FormatVA(format, ap);
    va_end(ap);

    // Get exception class, and throw the exception in javaland.
    jclass exceptionClass = env->FindClass("com/example/user/gbe/GBSystemException");
    env->ThrowNew(exceptionClass, message);
}

extern "C" jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return -1;

    GBSystem_Class = env->FindClass("com/example/user/gbe/GBSystem");
    if (GBSystem_Class == nullptr)
        return -1;

    // Create global reference to class.
    GBSystem_Class = static_cast<jclass>(env->NewGlobalRef(GBSystem_Class));
    if (GBSystem_Class == nullptr)
        return -1;

    if ((GBSystem_Field_NativePointer = env->GetFieldID(GBSystem_Class, "nativePointer", "J")) == nullptr ||
        (GBSystem_Method_OnScreenBufferReady = env->GetMethodID(GBSystem_Class, "onScreenBufferReady", "()V")) == nullptr ||
        (GBSystem_Method_LoadCartridgeRAM = env->GetMethodID(GBSystem_Class, "onLoadCartridgeRAM", "([BI)Z")) == nullptr ||
        (GBSystem_Method_SaveCartridgeRAM = env->GetMethodID(GBSystem_Class, "onSaveCartridgeRAM", "([BI)V")) == nullptr ||
        (GBSystem_Method_LoadCartridgeRTC = env->GetMethodID(GBSystem_Class, "onLoadCartridgeRTC", "([BI)Z")) == nullptr ||
        (GBSystem_Method_SaveCartridgeRTC = env->GetMethodID(GBSystem_Class, "onSaveCartridgeRTC", "([BI)V")) == nullptr)
    {
        env->DeleteGlobalRef(GBSystem_Class);
        return -1;
    }

    // Enable logging.
#ifdef NDEBUG
    g_pLog->SetDebugOutputParams(true, nullptr, LOGLEVEL_INFO);
#else
    g_pLog->SetDebugOutputParams(true, nullptr, LOGLEVEL_PROFILE);
#endif

    jvm = vm;
    return JNI_VERSION_1_6;
}

extern "C" void JNI_OnUnload(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return;

    if (GBSystem_Class != nullptr)
    {
        env->DeleteGlobalRef(GBSystem_Class);
        GBSystem_Class = nullptr;
    }

    jvm = nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeInit(JNIEnv *env, jobject obj)
{
    // Create reference to java object.
    jobject objRef = env->NewGlobalRef(obj);

    // allocate/set pointer
    GBSystemNative *native = new GBSystemNative(objRef);
    env->SetLongField(obj, GBSystem_Field_NativePointer, (jlong)(uintptr_t)native);
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeDestroy(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);

    // Release reference to java object.
    jobject objRef = native->GetJObject();
    Assert(env->IsSameObject(obj, objRef));
    env->DeleteGlobalRef(objRef);

    delete native;
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeLoadCartridge(JNIEnv *env, jobject obj, jbyteArray cartData)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    jbyte *localCartData = env->GetByteArrayElements(cartData, nullptr);
    size_t localCartDataLength = env->GetArrayLength(cartData);

    Error error;
    bool result = native->LoadCartridge((byte *)localCartData, localCartDataLength, &error);
    env->ReleaseByteArrayElements(cartData, localCartData, JNI_ABORT);
    if (!result)
        ThrowGBSystemException(env, "Cartridge load failed: %s", error.GetErrorCodeAndDescription().GetCharArray());
}

extern "C" JNIEXPORT jint JNICALL Java_com_example_user_gbe_GBSystem_nativeGetCartridgeMode(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    Cartridge *cart = native->GetCartridge();
    if (cart == nullptr)
    {
        ThrowGBSystemException(env, "No cartridge loaded.");
        return 0;
    }

    return (jint)cart->GetSystemMode();
}

extern "C" JNIEXPORT jstring JNICALL Java_com_example_user_gbe_GBSystem_nativeGetCartridgeName(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    Cartridge *cart = native->GetCartridge();
    if (cart == nullptr)
    {
        ThrowGBSystemException(env, "No cartridge loaded.");
        return nullptr;
    }

    return env->NewStringUTF(cart->GetName());
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeBootSystem(JNIEnv *env, jobject obj, jint systemMode)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    if (systemMode < 0 || systemMode >= NUM_SYSTEM_MODES)
    {
        ThrowGBSystemException(env, "Invalid system mode: %d", systemMode);
        return;
    }

    System *system = native->GetSystem();
    Cartridge *cart = native->GetCartridge();
    if (!system->Init((SYSTEM_MODE)systemMode, nullptr, 0, cart))
    {
        ThrowGBSystemException(env, "System boot failed.");
        return;
    }

    system->SetAccurateTiming(false);
    system->SetFrameLimiter(true);
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeSetPaused(JNIEnv *env, jobject obj, jboolean paused)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    native->GetSystem()->SetPaused((bool)paused);
}

extern "C" JNIEXPORT jdouble JNICALL Java_com_example_user_gbe_GBSystem_nativeExecuteFrame(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    return native->GetSystem()->ExecuteFrame();
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeCopyScreenBufferToBitmap(JNIEnv *env, jobject obj, jobject destinationBitmap)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    native->CopyFrameBufferToBitmap(env, destinationBitmap);
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeCopyScreenBufferToTexture(JNIEnv *env, jobject obj, jint glTextureId)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    native->CopyFrameBufferToGLTexture(glTextureId);
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeSetPadDirectionState(JNIEnv *env, jobject obj, jint state) {
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    native->GetSystem()->SetPadDirectionState((uint32)state);
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeSetPadButtonState(JNIEnv *env, jobject obj, jint state) {
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);
    native->GetSystem()->SetPadButtonState((uint32)state);
}

extern "C" JNIEXPORT jbyteArray JNICALL Java_com_example_user_gbe_GBSystem_nativeSaveState(JNIEnv *env, jobject obj) {
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);

    GrowableMemoryByteStream *pStream = ByteStream_CreateGrowableMemoryStream();
    if (!native->GetSystem()->SaveState(pStream)) {
        pStream->Release();
        ThrowGBSystemException(env, "Saving state failed");
        return nullptr;
    }

    uint32 size = (uint32)pStream->GetSize();
    jbyteArray retArray = env->NewByteArray(size);
    jbyte *retArrayData = env->GetByteArrayElements(retArray, nullptr);
    Y_memcpy(retArrayData, pStream->GetMemoryPointer(), size);
    env->ReleaseByteArrayElements(retArray, retArrayData, JNI_COMMIT);
    pStream->Release();
    return retArray;
}

extern "C" JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeLoadState(JNIEnv *env, jobject obj, jbyteArray data) {
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)env->GetLongField(obj, GBSystem_Field_NativePointer);

    jbyte *pData = env->GetByteArrayElements(data, nullptr);
    uint32 size = env->GetArrayLength(data);
    ByteStream *pStream = ByteStream_CreateReadOnlyMemoryStream(pData, size);

    Error error;
    if (!native->GetSystem()->LoadState(pStream, &error)) {
        pStream->Release();
        env->ReleaseByteArrayElements(data, pData, JNI_ABORT);
        ThrowGBSystemException(env, "Loading state failed: %s", error.GetErrorCodeAndDescription().GetCharArray());
        return;
    }

    pStream->Release();
    env->ReleaseByteArrayElements(data, pData, JNI_ABORT);
}

