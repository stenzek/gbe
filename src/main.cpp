#include <SDL/SDL.h>
#include "system.h"
#include "cartridge.h"
#include "display.h"
#include "YBaseLib/ByteStream.h"
#include "YBaseLib/AutoReleasePtr.h"
#include "YBaseLib/Error.h"
#include "YBaseLib/Log.h"
#include "YBaseLib/FileSystem.h"
#include <cstdio>
Log_SetChannel(Main);

struct ProgramArgs
{
    const char *bios_filename;
    const char *cart_filename;
};

struct State
{
    const Cartridge *cart;
    const byte *bios;

    System *system;

    SDL_Window *window;
    SDL_Surface *surface;

    bool running;
};

static bool LoadBIOS(const char *filename, bool specified, State *state)
{
    AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(filename, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
    if (pStream == nullptr)
    {
        if (!specified)
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

    state->cart = new Cartridge();
    Error error;
    if (!const_cast<Cartridge *>(state->cart)->Load(pStream, &error))
    {
        Log_ErrorPrintf("Failed to load cartridge file '%s': %s", filename, error.GetErrorDescription().GetCharArray());
        return false;
    }

    return true;
}

static bool ParseArguments(int argc, char *argv[], ProgramArgs *out_args)
{
    return false;
}

static bool InitializeState(const ProgramArgs *args, State *state)
{
    state->bios = nullptr;
    state->cart = nullptr;
    state->system = nullptr;
    state->window = nullptr;
    state->surface = nullptr;

    state->window = SDL_CreateWindow("gbe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, Display::SCREEN_WIDTH, Display::SCREEN_HEIGHT, 0);
    if (state->window == nullptr)
        return false;

    state->surface = SDL_GetWindowSurface(state->window);
    if (state->surface == nullptr)
        return false;

    state->system = new System();
    if (state->cart != nullptr)
        state->system->SetCartridge(state->cart);
    state->system->SetDisplaySurface(state->window, state->surface);

    state->system->Reset();

    return false;
}

static void CleanupState(State *state)
{
    delete[] state->bios;
    delete state->cart;
    delete state->system;
    if (state->window != nullptr)
        SDL_DestroyWindow(state->window);
}


static int Run(State *state)
{
    while (state->running)
    {
        SDL_PumpEvents();
        state->system->Step();
    }

    return 0;
}

// SDL requires the entry point declared without c++ decoration
extern "C" int main(int argc, char *argv[])
{
    // set log flags
    g_pLog->SetConsoleOutputParams(true);
    g_pLog->SetDebugOutputParams(true);

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
    SDL_Quit();
    return return_code;
}
