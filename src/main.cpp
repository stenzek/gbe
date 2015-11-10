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
#include <SDL/SDL.h>
#include <cstdio>
Log_SetChannel(Main);

struct ProgramArgs
{
    const char *bios_filename;
    const char *cart_filename;
    bool disable_bios;
    bool permissive_memory;
};

struct State : public System::CallbackInterface
{
    Cartridge *cart;
    const byte *bios;

    System *system;

    SDL_Window *window;
    SDL_Surface *surface;
    SDL_Surface *offscreen_surface;

    SDL_AudioDeviceID audio_device_id;

    String savestate_prefix;

    bool running;

    void SetSaveStatePrefix(const char *cartridge_file_name)
    {
        const char *last_part = Y_strrchr(cartridge_file_name, '/');
        if (last_part == nullptr || Y_strrchr(cartridge_file_name, '\\') > last_part)
            last_part = Y_strrchr(cartridge_file_name, '\\');
        if (last_part != nullptr)
            last_part++;
        else
            last_part = cartridge_file_name;

        SmallString savestate_prefix_filepart;
        savestate_prefix_filepart.AppendString("saves/");
        savestate_prefix_filepart.AppendString(last_part);
        if (savestate_prefix_filepart.RFind('.') > 0)
            savestate_prefix_filepart.Erase(savestate_prefix_filepart.RFind('.'));
        
        Platform::GetProgramFileName(savestate_prefix);
        FileSystem::BuildPathRelativeToFile(savestate_prefix, savestate_prefix, savestate_prefix_filepart, true, true);
    }


    static void AudioCallback(void *pThis, uint8 *stream, int length)
    {
        State *pState = (State *)pThis;
        Audio *audio = pState->system->GetAudio();
        int16 *samples = (int16 *)stream;
        size_t nsamples = length / 2;

        size_t i = audio->ReadSamples(samples, nsamples);
        if (i < nsamples)
            Y_memzero(samples + i, (nsamples - i) * 2);
    }

    void SetScale(uint32 scale)
    {
        scale = Max(scale, (uint32)1);

        if (offscreen_surface != nullptr)
        {
            SDL_FreeSurface(offscreen_surface);
            offscreen_surface = nullptr;
        }

        if (scale > 1)
        {
            offscreen_surface = SDL_CreateRGBSurface(0, 160, 144, 32, 0xff, 0xff00, 0xff0000, 0);
            DebugAssert(offscreen_surface != nullptr);
        }

        SDL_SetWindowSize(window, 160 * scale, 144 * scale);
        surface = SDL_GetWindowSurface(window);
    }

    bool LoadState(uint32 index)
    {
        SmallString filename;
        filename.Format("%s_%02u.savestate", savestate_prefix.GetCharArray(), index);
        Log_DevPrintf("Savestate filename: '%s'", filename.GetCharArray());

        ByteStream *pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
        if (pStream == nullptr)
        {
            Log_ErrorPrintf("Failed to load state '%s': could not open file", filename.GetCharArray());
            return false;
        }

        Error error;
        if (!system->LoadState(pStream, &error))
        {
            Log_ErrorPrintf("Failed to save state '%s': load error: %s", filename.GetCharArray(), error.GetErrorCodeAndDescription().GetCharArray());
            pStream->Release();
            return false;
        }

        Log_InfoPrintf("Save state '%s' loaded.", filename.GetCharArray());
        pStream->Release();
        return true;
    }

    bool SaveState(uint32 index)
    {
        SmallString filename;
        filename.Format("%s_%02u.savestate", savestate_prefix.GetCharArray(), index);
        Log_DevPrintf("Savestate filename: '%s'", filename.GetCharArray());

        ByteStream *pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_CREATE_PATH | BYTESTREAM_OPEN_WRITE | BYTESTREAM_OPEN_TRUNCATE | BYTESTREAM_OPEN_STREAMED | BYTESTREAM_OPEN_ATOMIC_UPDATE);
        if (pStream == nullptr)
        {
            Log_ErrorPrintf("Failed to save state '%s': could not open file", filename.GetCharArray());
            return false;
        }

        if (!system->SaveState(pStream))
        {
            Log_ErrorPrintf("Failed to save state '%s': save error", filename.GetCharArray());
            pStream->Discard();
            pStream->Release();
            return false;
        }

        Log_InfoPrintf("Save state '%s' saved.", filename.GetCharArray());
        pStream->Commit();
        pStream->Release();
        return true;
    }

    // Callback to present a frame
    virtual void PresentDisplayBuffer(const void *pixels, uint32 row_stride) override final
    {
        if (offscreen_surface != nullptr)
        {
            if (SDL_MUSTLOCK(offscreen_surface))
                SDL_LockSurface(offscreen_surface);

            if (row_stride == (uint32)offscreen_surface->pitch)
                Y_memcpy(offscreen_surface->pixels, pixels, row_stride * Display::SCREEN_HEIGHT);
            else
                Y_memcpy_stride(offscreen_surface->pixels, offscreen_surface->pitch, pixels, row_stride, sizeof(uint32) * Display::SCREEN_WIDTH, Display::SCREEN_HEIGHT);

            if (SDL_MUSTLOCK(surface))
                SDL_LockSurface(surface);

            SDL_BlitScaled(offscreen_surface, nullptr, surface, nullptr);

            if (SDL_MUSTLOCK(surface))
                SDL_UnlockSurface(surface);

            if (SDL_MUSTLOCK(offscreen_surface))
                SDL_UnlockSurface(offscreen_surface);
        }
        else
        {
            if (SDL_MUSTLOCK(surface))
                SDL_LockSurface(surface);

            const byte *pBytePixels = reinterpret_cast<const byte *>(pixels);
            for (uint32 y = 0; y < Display::SCREEN_HEIGHT; y++)
            {
                const byte *inLine = pBytePixels + (y * row_stride);
                uint32 *outLine = (uint32 *)((byte *)surface->pixels + (y * (uint32)surface->pitch));

                for (uint32 x = 0; x < Display::SCREEN_WIDTH; x++)
                {
                    *(outLine++) = SDL_MapRGB(surface->format, inLine[0], inLine[1], inLine[2]);
                    inLine += 4;
                }
            }

            if (SDL_MUSTLOCK(surface))
                SDL_UnlockSurface(surface);
        }

        SDL_UpdateWindowSurface(window);
    }

    virtual bool LoadCartridgeRAM(void *pData, size_t expected_data_size) override final
    {
        SmallString filename;
        filename.Format("%s.sram", savestate_prefix.GetCharArray());
        Log_DevPrintf("Cartridge SRAM filename: '%s'", filename.GetCharArray());

        AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
        if (pStream == nullptr)
            return false;

        if (pStream->GetSize() != (uint64)expected_data_size)
        {
            Log_WarningPrintf("External ram size mismatch (expecting %u, got %u)", (uint32)expected_data_size, (uint32)pStream->GetSize());
            return false;
        }

        BinaryReader binaryReader(pStream);
        if (!binaryReader.SafeReadBytes(pData, expected_data_size))
        {
            Log_ErrorPrintf("Read error");
            return false;
        }

        return true;
    }

    virtual void SaveCartridgeRAM(const void *pData, size_t data_size) override final
    {
        SmallString filename;
        filename.Format("%s.sram", savestate_prefix.GetCharArray());
        Log_DevPrintf("Cartridge SRAM filename: '%s'", filename.GetCharArray());
        
        ByteStream *pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_CREATE_PATH | BYTESTREAM_OPEN_WRITE | BYTESTREAM_OPEN_TRUNCATE | BYTESTREAM_OPEN_ATOMIC_UPDATE | BYTESTREAM_OPEN_STREAMED);
        if (pStream == nullptr)
        {
            Log_ErrorPrint("Failed to write sram: Failed to open file");
            return;
        }

        if (!pStream->Write2(pData, data_size) || 
            !pStream->Commit())
        {
            Log_ErrorPrintf("Failed to write sram: Failed to write file");
            pStream->Discard();
            pStream->Release();
            return;
        }

        pStream->Release();
    }

    virtual bool LoadCartridgeRTC(void *pData, size_t expected_data_size) override final
    {
        SmallString filename;
        filename.Format("%s.rtc", savestate_prefix.GetCharArray());
        Log_DevPrintf("Cartridge RTC filename: '%s'", filename.GetCharArray());

        AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
        if (pStream == nullptr)
            return false;

        if (pStream->GetSize() != (uint64)expected_data_size)
        {
            Log_WarningPrintf("RTC data size mismatch (expecting %u, got %u)", (uint32)expected_data_size, (uint32)pStream->GetSize());
            return false;
        }

        BinaryReader binaryReader(pStream);
        if (!binaryReader.SafeReadBytes(pData, expected_data_size))
        {
            Log_ErrorPrintf("Read error");
            return false;
        }

        return true;
    }

    virtual void SaveCartridgeRTC(const void *pData, size_t data_size) override final
    {
        SmallString filename;
        filename.Format("%s.rtc", savestate_prefix.GetCharArray());
        Log_DevPrintf("Cartridge RTC filename: '%s'", filename.GetCharArray());

        ByteStream *pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_CREATE | BYTESTREAM_OPEN_CREATE_PATH | BYTESTREAM_OPEN_WRITE | BYTESTREAM_OPEN_TRUNCATE | BYTESTREAM_OPEN_ATOMIC_UPDATE | BYTESTREAM_OPEN_STREAMED);
        if (pStream == nullptr)
        {
            Log_ErrorPrint("Failed to write RTC data: Failed to open file");
            return;
        }

        if (!pStream->Write2(pData, data_size) ||
            !pStream->Commit())
        {
            Log_ErrorPrintf("Failed to write RTC data: Failed to write file");
            pStream->Discard();
            pStream->Release();
            return;
        }

        pStream->Release();
    }
};

static bool LoadBIOS(const char *filename, bool specified, State *state)
{
    AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
    if (pStream == nullptr)
    {
        if (specified)
            Log_ErrorPrintf("Failed to load bios file '%s'", filename);

        return false;
    }

    if (pStream->GetSize() != GB_BIOS_LENGTH)
    {
        Log_ErrorPrintf("Bios file '%s' is incorrect length (%u bytes, should be %u bytes)", filename, (uint32)pStream->GetSize(), GB_BIOS_LENGTH);
        return false;
    }

    state->bios = new byte[GB_BIOS_LENGTH];
    if (!pStream->Read2((byte *)state->bios, GB_BIOS_LENGTH))
    {
        Log_ErrorPrintf("Failed to read bios file '%s'", filename);
        return false;
    }

    Log_InfoPrintf("Loaded bios file '%s'.", filename);
    return true;
}

static bool LoadCart(const char *filename, State *state)
{
    AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
    if (pStream == nullptr)
    {
        Log_ErrorPrintf("Failed to open cartridge file '%s'", filename);
        return false;
    }

    state->SetSaveStatePrefix(filename);
    state->cart = new Cartridge(state->system);
    Error error;
    if (!state->cart->Load(pStream, &error))
    {
        Log_ErrorPrintf("Failed to load cartridge file '%s': %s", filename, error.GetErrorDescription().GetCharArray());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cart load error", error.GetErrorCodeAndDescription(), nullptr);
        return false;
    }

    return true;
}

static void ShowUsage(const char *progname)
{
    fprintf(stderr, "gbe\n");
    fprintf(stderr, "usage: %s [-h] [-bios <bios file>] [-nobios] [-permissivememory] [cart file]\n", progname);

}

static bool ParseArguments(int argc, char *argv[], ProgramArgs *out_args)
{
#define CHECK_ARG(str) !Y_strcmp(argv[i], str)
#define CHECK_ARG_PARAM(str) !Y_strcmp(argv[i], str) && ((i + 1) < argc)

    out_args->bios_filename = nullptr;
    out_args->cart_filename = nullptr;
    out_args->disable_bios = false;
    out_args->permissive_memory = true;

    for (int i = 1; i < argc; i++)
    {
        if (CHECK_ARG("-h") || CHECK_ARG("-?"))
        {
            ShowUsage(argv[0]);
            return false;
        }
        else if (CHECK_ARG_PARAM("-bios"))
        {
            out_args->bios_filename = argv[++i];
        }
        else if (CHECK_ARG("-nobios"))
        {
            out_args->disable_bios = true;
        }
        else if (CHECK_ARG("-permissivememory"))
        {
            out_args->permissive_memory = true;
        }
        else if (CHECK_ARG("-nopermissivememory"))
        {
            out_args->permissive_memory = false;
        }
        else
        {
            out_args->cart_filename = argv[i];
        }
    }

    return true;

#undef CHECK_ARG
#undef CHECK_ARG_PARAM
}

static bool InitializeState(const ProgramArgs *args, State *state)
{
    state->bios = nullptr;
    state->cart = nullptr;
    state->system = nullptr;
    state->window = nullptr;
    state->surface = nullptr;
    state->offscreen_surface = nullptr;
    state->audio_device_id = 0;
    state->running = true;

    // load bios
    bool bios_specified = (args->bios_filename != nullptr);
    const char *bios_filename = (args->bios_filename != nullptr) ? args->bios_filename : "bios.bin";
    if (!args->disable_bios && !LoadBIOS(bios_filename, bios_specified, state) && bios_specified)
        return false;

    // load cart
    state->system = new System(state);
    if (args->cart_filename != nullptr && !LoadCart(args->cart_filename, state))
    {
        delete state->system;
        return false;
    }

    // create render window
    SmallString window_title;
    window_title.Format("gbe - %s", state->cart->GetName().GetCharArray());
    state->window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, Display::SCREEN_WIDTH, Display::SCREEN_HEIGHT, 0);
    if (state->window == nullptr)
    {
        Log_ErrorPrintf("Failed to crate SDL window: %s", SDL_GetError());
        delete state->cart;
        delete state->system;
        return false;
    }

    // get surface to draw to
    state->surface = SDL_GetWindowSurface(state->window);
    if (state->surface == nullptr)
    {
        SDL_DestroyWindow(state->window);
        delete state->cart;
        delete state->system;
        return false;
    }

    // create audio device
    SDL_AudioSpec audio_spec = { 44100, AUDIO_S16, 2, 0, 2048, 0, 0, &State::AudioCallback, (void *)state };
    SDL_AudioSpec obtained_audio_spec;
    state->audio_device_id = SDL_OpenAudioDevice(nullptr, 0, &audio_spec, &obtained_audio_spec, 0);
    if (state->audio_device_id == 0)
        Log_WarningPrintf("Failed to open audio device (error: %s). No audio will be heard.", SDL_GetError());

    // init system
    if (!state->system->Init(NUM_SYSTEM_MODES, state->bios, state->cart))
    {
        Log_ErrorPrintf("Failed to initialize system");
        return false;
    }

    // apply options
    state->system->SetPermissiveMemoryAccess(args->permissive_memory);
    //state->system->SetAccurateTiming(false);
    //state->system->SetAudioEnabled(false);
    return true;
}

static void CleanupState(State *state)
{
    delete[] state->bios;
    delete state->cart;
    delete state->system;

    if (state->offscreen_surface != nullptr)
        SDL_FreeSurface(state->offscreen_surface);

    if (state->audio_device_id != 0)
        SDL_CloseAudioDevice(state->audio_device_id);

    if (state->window != nullptr)
        SDL_DestroyWindow(state->window);
}


static int Run(State *state)
{
    Timer time_since_last_report;

    // resume audio
    if (state->audio_device_id != 0)
        SDL_PauseAudioDevice(state->audio_device_id, 0);

    while (state->running)
    {
        SDL_PumpEvents();
        for (;;)
        {
            SDL_Event events[16];
            int nevents = SDL_PeepEvents(events, countof(events), SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (nevents == 0)
                break;

            for (int i = 0; i < nevents; i++)
            {
                const SDL_Event *event = events + i;
                switch (event->type)
                {
                case SDL_QUIT:
                    state->running = false;
                    break;

                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    {
                        bool down = (event->type == SDL_KEYDOWN);
                        switch (event->key.keysym.sym)
                        {
                        case SDLK_w:
                        case SDLK_UP:
                            state->system->SetPadDirection(PAD_DIRECTION_UP, down);
                            break;

                        case SDLK_a:
                        case SDLK_LEFT:
                            state->system->SetPadDirection(PAD_DIRECTION_LEFT, down);
                            break;

                        case SDLK_s:
                        case SDLK_DOWN:
                            state->system->SetPadDirection(PAD_DIRECTION_DOWN, down);
                            break;

                        case SDLK_d:
                        case SDLK_RIGHT:
                            state->system->SetPadDirection(PAD_DIRECTION_RIGHT, down);
                            break;

                        case SDLK_z:
                            state->system->SetPadButton(PAD_BUTTON_B, down);
                            break;

                        case SDLK_x:
                            state->system->SetPadButton(PAD_BUTTON_A, down);
                            break;

                        case SDLK_c:
                            state->system->SetPadButton(PAD_BUTTON_SELECT, down);
                            break;

                        case SDLK_v:
                            state->system->SetPadButton(PAD_BUTTON_START, down);
                            break;

                        case SDLK_1:
                        case SDLK_2:
                        case SDLK_3:
                        case SDLK_4:
                        case SDLK_5:
                        case SDLK_6:
                        case SDLK_7:
                        case SDLK_8:
                        case SDLK_9:
                            {
                                if (!down)
                                    state->SetScale((event->key.keysym.sym - SDLK_1) + 1);

                                break;
                            }

                        case SDLK_KP_MULTIPLY:
                            {
                                if (!down)
                                {
                                    state->system->SetAudioEnabled(!state->system->GetAudioEnabled());
                                    Log_DevPrintf("Audio is now %s", state->system->GetAudioEnabled() ? "enabled" : "disabled");
                                }

                                break;
                            }

                        case SDLK_KP_PLUS:
                            {
                                if (down)
                                {
                                    state->system->SetTargetSpeed(state->system->GetTargetSpeed() + 0.25f);
                                    Log_DevPrintf("Target speed set to %.2f%%", state->system->GetTargetSpeed() * 100.0f);
                                }

                                break;
                            }

                        case SDLK_KP_MINUS:
                            {
                                if (down)
                                {
                                    state->system->SetTargetSpeed(state->system->GetTargetSpeed() - 0.25f);
                                    Log_DevPrintf("Target speed set to %.2f%%", state->system->GetTargetSpeed() * 100.0f);
                                }

                                break;
                            }

                        case SDLK_KP_PERIOD:
                            {
                                if (!down)
                                {
                                    state->system->SetAccurateTiming(!state->system->GetAccurateTiming());
                                    Log_DevPrintf("Set accurate timing %s", state->system->GetAccurateTiming() ? "on" : "off");
                                }

                                break;
                            }

                        case SDLK_KP_ENTER:
                            {
                                if (!down)
                                {
                                    state->system->SetFrameLimiter(!state->system->GetFrameLimiter());
                                    Log_DevPrintf("Set framelimiter %s", state->system->GetFrameLimiter() ? "on" : "off");
                                }

                                break;
                            }

                        case SDLK_F1:
                        case SDLK_F2:
                        case SDLK_F3:
                        case SDLK_F4:
                        case SDLK_F5:
                        case SDLK_F6:
                        case SDLK_F7:
                        case SDLK_F8:
                        case SDLK_F9:
                        case SDLK_F10:
                        case SDLK_F11:
                        case SDLK_F12:
                            {
                                if (!down)
                                {
                                    uint32 index = event->key.keysym.sym - SDLK_F1 + 1;
                                    if (event->key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
                                        state->SaveState(index);
                                    else
                                        state->LoadState(index);
                                }

                                break;
                            }

                        case SDLK_PAUSE:
                            {
                                if (!down)
                                {
                                    if (event->key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
                                    {
                                        Log_DevPrintf("Resetting system.");
                                        state->system->Reset();
                                    }
                                    else
                                    {
                                        if (state->system->GetPaused())
                                        {
                                            Log_DevPrintf("Resuming emulation.");
                                            state->system->SetPaused(false);
                                        }
                                        else
                                        {
                                            Log_DevPrintf("Pausing emulation.");
                                            state->system->SetPaused(true);
                                        }
                                    }
                                }
                                break;
                            }

                        case SDLK_HOME:
                            {
                                if (down && event->key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
                                {
                                    Log_DevPrintf("Hosting link server.");
                                    
                                    Error error;
                                    if (!LinkConnectionManager::GetInstance().Host("0.0.0.0", 1337, &error))
                                        Log_ErrorPrintf("  Failed: %s", error.GetErrorCodeAndDescription().GetCharArray());
                                }

                                break;
                            }

                        case SDLK_END:
                            {
                                if (down && event->key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
                                {
                                    Log_DevPrintf("Connecting to link server.");
                                    state->system->SetPaused(true);

                                    Error error;
                                    if (!LinkConnectionManager::GetInstance().Connect("127.0.0.1", 1337, &error))
                                        Log_ErrorPrintf("  Failed: %s", error.GetErrorCodeAndDescription().GetCharArray());

                                    state->system->SetPaused(false);
                                }

                                break;
                            }
                        }

                        break;
                    }
                }
            }
        }

        // run a frame
        double sleep_time_seconds = state->system->ExecuteFrame();
        if (sleep_time_seconds >= 0.001)
        {
            // round down to the next millisecond (fix when usleep is implemented)
            uint32 sleep_time_ms = (uint32)std::floor(sleep_time_seconds * 1000.0);
            Thread::Sleep(sleep_time_ms);
        }

        // report statistics
        if (time_since_last_report.GetTimeSeconds() > 1.0)
        {
            Log_DevPrintf("Current frame: %u, emulation speed: %.3f%%, target emulation speed: %.3f%%", state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f, state->system->GetTargetSpeed() * 100.0f);
            time_since_last_report.Reset();

            // update window title
            SmallString window_title;
            window_title.Format("gbe - %s - Frame %u - %.0f%%", state->cart->GetName().GetCharArray(), state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f);
            SDL_SetWindowTitle(state->window, window_title);
        }
    }

    // pause audio
    if (state->audio_device_id != 0)
        SDL_PauseAudioDevice(state->audio_device_id, 1);

    return 0;
}

// SDL requires the entry point declared without c++ decoration
extern "C" int main(int argc, char *argv[])
{
    // set log flags
    //g_pLog->SetConsoleOutputParams(true, nullptr, LOGLEVEL_TRACE);
    g_pLog->SetConsoleOutputParams(true, nullptr, LOGLEVEL_PROFILE);
    //g_pLog->SetConsoleOutputParams(true, "CPU Cartridge System");
    //g_pLog->SetDebugOutputParams(true);

#if defined(__WIN32__)
    // fix up stdout/stderr on win32
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

    // init sdl
    if (SDL_Init(0) < 0)
    {
        Panic("SDL initialization failed");
        return -1;
    }

    // init sdl audio
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
        Log_WarningPrintf("Failed to initialize SDL audio subsystem: %s", SDL_GetError());

    // parse args
    ProgramArgs args;
    if (!ParseArguments(argc, argv, &args))
        return 1;

    // init state
    State state;
    if (!InitializeState(&args, &state))
    {
        CleanupState(&state);
        return 2;
    }

    // run
    int return_code = Run(&state);

    // cleanup
    CleanupState(&state);
    LinkConnectionManager::GetInstance().Shutdown();
    SDL_Quit();
    return return_code;
}
