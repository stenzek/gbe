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
Log_SetChannel(GBESystemNative);

// method ids for callbacks
static JavaVM *jvm;
static jclass GBSystem_Class;
static jclass GBSystemException_Class;
static jfieldID GBSystem_Field_NativePointer;
static jmethodID GBSystem_Method_PresentDisplayBuffer;
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
    {
        m_system = new System(this);
    }

    ~GBSystemNative()
    {
        delete m_cart;
        delete m_system;
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
        //if (!m_cart->Load())
        return false;
    }

    bool BootSystem(SYSTEM_MODE mode, const byte *bios, uint32 bios_length)
    {
        return m_system->Init(mode, nullptr, m_cart);
    }

    virtual void PresentDisplayBuffer(const void *pPixels, uint32 row_stride) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return;

        jbyteArray *pixelsArray = (*env)->NewByteArray(row_stride * Display::SCREEN_HEIGHT);
        jbyte *localPixelsArray = (*env)->GetByteArrayElements(env, pixelsArray, nullptr);
        Y_memcpy(localPixelsArray, pPixels, row_stride * Display::SCREEN_HEIGHT);
        (*env)->ReleaseByteArrayElements(env, pixelsArray, localPixelsArray, JNI_COMMIT);

        (*env)->CallVoidMethod(env, m_jobject, GBSystem_Method_PresentDisplayBuffer, pixelsArray, (int)row_stride);
    }

    virtual bool LoadCartridgeRAM(void *pData, size_t expected_data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return false;

        jbyteArray resultData = (*env)->CallObjectMethod(env, m_jobject, GBSystem_Method_LoadCartridgeRAM, (int)expected_data_size);
        if (resultData == nullptr)
            return false;

        size_t localDataSize = (*env)->GetArrayLength(env, resultData);
        if (localDataSize != expected_data_size)
            return false;

        jbyte *localData = (*env)->GetByteArrayElements(env, resultData, nullptr);
        Y_memcpy(pData, localData, expected_data_size);
        (*env)->ReleaseByteArrayElements(env, resultData, localData, JNI_ABORT);
        return true;
    }

    virtual void SaveCartridgeRAM(const void *pData, size_t data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return;

        jbyteArray *dataArray = (*env)->NewByteArray(data_size);
        jbyte *localDataArray = (*env)->GetByteArrayElements(env, dataArray, nullptr);
        Y_memcpy(localDataArray, pData, data_size);
        (*env)->ReleaseByteArrayElements(env, dataArray, localDataArray, JNI_COMMIT);

        (*env)->CallVoidMethod(env, m_jobject, GBSystem_Method_SaveCartridgeRAM, dataArray, (int)row_stride);
    }

    virtual bool LoadCartridgeRTC(void *pData, size_t expected_data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return false;

        jbyteArray resultData = (*env)->CallObjectMethod(env, m_jobject, GBSystem_Method_LoadCartridgeRTC, (int)expected_data_size);
        if (resultData == nullptr)
            return false;

        size_t localDataSize = (*env)->GetArrayLength(env, resultData);
        if (localDataSize != expected_data_size)
            return false;

        jbyte *localData = (*env)->GetByteArrayElements(env, resultData, nullptr);
        Y_memcpy(pData, localData, expected_data_size);
        (*env)->ReleaseByteArrayElements(env, resultData, localData, JNI_ABORT);
        return true;
    }

    virtual void SaveCartridgeRTC(const void *pData, size_t data_size) override final
    {
        JNIEnv *env = GetJNIEnv();
        if (env == nullptr)
            return;

        jbyteArray *dataArray = (*env)->NewByteArray(data_size);
        jbyte *localDataArray = (*env)->GetByteArrayElements(env, dataArray, nullptr);
        Y_memcpy(localDataArray, pData, data_size);
        (*env)->ReleaseByteArrayElements(env, dataArray, localDataArray, JNI_COMMIT);

        (*env)->CallVoidMethod(env, m_jobject, GBSystem_Method_SaveCartridgeRTC, dataArray, (int)row_stride);
    }

private:
    jobject m_jobject;
    Cartridge *m_cart;
    System *m_system;
};

static void ThrowGBSystemException(JNIEnv *env, const char *format, ...)
{
    SmallString message;
    va_list ap;
    va_start(ap, format);
    message.FormatVA(format, ap);
    va_end(ap);

    (*env)->ThrowNew(env, GBSystemException_Class, message);
}

jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return -1;

    GBSystem_Class = (*env)->FindClass(env, "com/example/user/gbe/GBSystem");
    if (GBSystem_Class == nullptr)
        return -1;

    if (GBSystem_Field_NativePointer = (*env)->GetFieldID(env, GBSystem_Class, "nativePointer", "J") == nullptr ||
        GBSystem_Method_PresentDisplayBuffer = (*env)->GetMethodID(env, GBSystem_Class, "onPresentDisplayBuffer", "([BI)V") ||
        GBSystem_Method_LoadCartridgeRAM = (*env)->GetMethodID(env, GBSystem_Class, "onLoadCartridgeRAM", "([BI)Z") ||
        GBSystem_Method_SaveCartridgeRAM = (*env)->GetMethodID(env, GBSystem_Class, "onSaveCartridgeRAM", "([BI)V") ||
        GBSystem_Method_LoadCartridgeRTC = (*env)->GetMethodID(env, GBSystem_Class, "onLoadCartridgeRTC", "([BI)Z") ||
        GBSystem_Method_SaveCartridgeRTC = (*env)->GetMethodID(env, GBSystem_Class, "onSaveCartridgeRTC", "([BI)V"))
    {
        return -1;
    }

    jvm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeInit(JNIEnv *env, jobject obj)
{
    // allocate/set pointer
    GBSystemNative *native = new GBSystemNative(obj);
    (*env)->SetLongField(env, obj, GBSystem_Field_NativePointer, (jlong)native);
    return true;
}

JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_nativeDestroy(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    delete native;
}

JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_loadCartridge(JNIEnv *env, jobject obj, jbyteArray cartData)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    jbyte *localCartData = (*env)->GetByteArrayElements(env, cartData, nullptr);
    size_t localCartDataLength = (*env)->GetArrayLength(env, cartData);

    Error error;
    bool result = native->LoadCartridge(localCartData, localCartDataLength, &error);
    (*env)->ReleaseByteArrayElements(env, cartData, localCartData, JNI_ABORT);
    if (!result)
        ThrowGBSystemException(env, "Cartridge load failed: %s", error.GetErrorCodeAndDescription().GetCharArray());
}

JNIEXPORT jint JNICALL Java_com_example_user_gbe_GBSystem_getCartridgeMode(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    Cartridge *cart = native->GetCartridge();
    if (cart == nullptr)
    {
        ThrowGBSystemException(env, "No cartridge loaded.");
        return 0;
    }

    return (jint)cart->GetSystemMode();
}

JNIEXPORT jstring JNICALL Java_com_example_user_gbe_GBSystem_getCartridgeName(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    Cartridge *cart = native->GetCartridge();
    if (cart == nullptr)
    {
        ThrowGBSystemException(env, "No cartridge loaded.");
        return nullptr;
    }

    return (*env)->NewStringUTF(cart->GetName());
}

JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_bootSystem(JNIEnv *env, jobject obj, jint systemMode)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    if (systemMode < 0 || systemMode >= NUM_SYSTEM_MODES)
    {
        ThrowGBSystemException(env, "Invalid system mode: %d", systemMode);
        return;
    }

    bool result = native->BootSystem((SYSTEM_MODE)systemMode, nullptr, 0);
    if (!result)
    {
        ThrowGBSystemException(env, "System boot failed.");
        return;
    }
}

JNIEXPORT jboolean JNICALL Java_com_example_user_gbe_GBSystem_isPaused(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    return native->GetSystem()->GetPaused();
}

JNIEXPORT void JNICALL Java_com_example_user_gbe_GBSystem_setPaused(JNIEnv *env, jobject obj, jboolean paused)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    native->GetSystem()->SetPaused((bool)paused);
}

JNIEXPORT jdouble JNICALL Java_com_example_user_gbe_GBSystem_executeFrame(JNIEnv *env, jobject obj)
{
    GBSystemNative *native = (GBSystemNative *)(uintptr_t)(*env)->GetLongField(env, obj, GBSystem_Field_NativePointer);
    return native->GetSystem()->ExecuteFrame();
}

