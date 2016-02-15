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
#include "YRenderLib/Renderer.h"
#include "YRenderLib/ImGui/ImGuiBridge.h"
#include "YRenderLib/ShaderCompiler/ShaderCompiler.h"
#include <SDL.h>
#include <hqx.h>
#include <cstdio>
Log_SetChannel(Main);

struct ProgramArgs
{
    const char *bios_filename;
    const char *cart_filename;
    bool disable_bios;
    bool permissive_memory;
    bool accurate_timing;
    bool frame_limiter;
    bool enable_audio;
};

struct State : public System::CallbackInterface
{
    Cartridge *cart;
    const byte *bios;
    uint32 bios_length;

    System *system;

    RendererOutputWindow *window;
    GPUDevice* gpu_device;
    GPUContext* gpu_context;
    GPUShaderProgram* gpu_program;
    GPUTexture2D* gpu_texture;

    uint32 gpu_texture_width;
    uint32 gpu_texture_height;
    byte* hq_texture_buffer;
    uint32 hq_texture_buffer_stride;
    uint32 hq_scale;

    SDL_AudioDeviceID audio_device_id;

    String savestate_prefix;

    bool running;

    bool needs_redraw;

    bool show_info_window;

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

    void SetHQScale(uint32 scale)
    {
        scale = Math::Clamp(scale, 1u, 4u);
        if (hq_scale == scale)
            return;

        GPU_TEXTURE2D_DESC desc(Display::SCREEN_WIDTH * scale, Display::SCREEN_HEIGHT * scale, PIXEL_FORMAT_R8G8B8A8_UNORM, GPU_TEXTURE_FLAG_SHADER_BINDABLE | GPU_TEXTURE_FLAG_WRITABLE, 1);
        GPU_SAMPLER_STATE_DESC sampler_desc(TEXTURE_FILTER_MIN_MAG_LINEAR_MIP_POINT, TEXTURE_ADDRESS_MODE_CLAMP, TEXTURE_ADDRESS_MODE_CLAMP, TEXTURE_ADDRESS_MODE_CLAMP, FloatColor::Black, 0.0f, 0, 0, 1, GPU_COMPARISON_FUNC_ALWAYS);
        GPUTexture2D* new_texture = gpu_device->CreateTexture2D(&desc, &sampler_desc);
        if (new_texture == nullptr)
            return;

        SAFE_RELEASE(gpu_texture);
        gpu_texture_width = desc.Width;
        gpu_texture_height = desc.Height;
        gpu_texture = new_texture;
        delete[] hq_texture_buffer;
        hq_texture_buffer = nullptr;
        hq_texture_buffer_stride = 0;
        hq_scale = scale;

        if (hq_scale > 1)
        {
            // only alloc buffer for >1x
            hq_texture_buffer_stride = PixelFormat_CalculateRowPitch(PIXEL_FORMAT_R8G8B8A8_UNORM, gpu_texture_width);
            hq_texture_buffer = new byte[hq_texture_buffer_stride * desc.Height];
        }

        // resize output window?
    }

    void DrawImGui()
    {
        bool boolOption;

        if (ImGui::BeginPopupContextVoid())
        {
            ImGui::MenuItem("Show Info Overlay", nullptr, &show_info_window);

            ImGui::Separator();

            if (ImGui::BeginMenu("Load State"))
            {
                for (uint32 i = 1; i < 10; i++)
                {
                    SmallString label;
                    label.Format("State %u", i);
                    if (ImGui::MenuItem(label))
                        LoadState(i);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Save State"))
            {
                for (uint32 i = 1; i < 10; i++)
                {
                    SmallString label;
                    label.Format("State %u", i);
                    if (ImGui::MenuItem(label))
                        SaveState(i);
                }

                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Reset"))
                system->Reset();

            ImGui::Separator();

            boolOption = system->GetAudioEnabled();
            if (ImGui::MenuItem("Enable Audio", nullptr, &boolOption))
                system->SetAudioEnabled(boolOption);

            boolOption = system->GetAccurateTiming();
            if (ImGui::MenuItem("Accurate Timing", nullptr, &boolOption))
                system->SetAccurateTiming(boolOption);

            boolOption = system->GetFrameLimiter();
            if (ImGui::MenuItem("Frame Limiter", nullptr, &boolOption))
                system->SetFrameLimiter(boolOption);

            ImGui::Separator();

            if (ImGui::BeginMenu("HQ Scaling"))
            {
                if (ImGui::MenuItem("1x", nullptr, (hq_scale == 1)))
                    SetHQScale(1);

                if (ImGui::MenuItem("2x", nullptr, (hq_scale == 2)))
                    SetHQScale(2);

                if (ImGui::MenuItem("3x", nullptr, (hq_scale == 3)))
                    SetHQScale(3);

                if (ImGui::MenuItem("4x", nullptr, (hq_scale == 4)))
                    SetHQScale(4);

                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Quit"))
                running = false;

            ImGui::EndPopup();
        }

        if (show_info_window)
        {
            ImGui::SetNextWindowPos(ImVec2(4.0f, 4.0f), ImGuiSetCond_FirstUseEver);

            if (ImGui::Begin("", &show_info_window, ImVec2(148.0f, 48.0f), 0.5f, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::Text("Frame %u (%.0f%%)", system->GetFrameCounter() + 1, system->GetCurrentSpeed() * 100.0f);
                ImGui::Text("%.2f FPS", system->GetCurrentFPS());
                ImGui::End();
            }
        }
    }

    void Redraw()
    {
        gpu_context->SetFullViewport();
        gpu_context->ClearTargets(true, true, false, FloatColor::Black);

        RENDERER_VIEWPORT draw_viewport(0, 0, 0, 0, 0.0f, 1.0f);
        uint32 width = window->GetWidth();
        uint32 height = window->GetHeight();
        if ((width * Display::SCREEN_HEIGHT / Display::SCREEN_WIDTH) > height)
        {
            // same AR as gameboy, add borders on left/right
            draw_viewport.Width = height * Display::SCREEN_WIDTH / Display::SCREEN_HEIGHT;
            draw_viewport.Height = height;
        }
        else
        {
            // draw borders on top/bottom
            draw_viewport.Width = width;
            draw_viewport.Height = width * Display::SCREEN_HEIGHT / Display::SCREEN_WIDTH;
        }
        draw_viewport.TopLeftX = (width - draw_viewport.Width) / 2;
        draw_viewport.TopLeftY = (height - draw_viewport.Height) / 2;
        gpu_context->SetViewport(&draw_viewport);

        gpu_context->SetRasterizerState(nullptr);
        gpu_context->SetDepthStencilState(nullptr, 0);
        gpu_context->SetBlendState(nullptr);
        gpu_context->SetInputLayout(nullptr);
        gpu_context->SetShaderProgram(gpu_program);
        gpu_context->SetShaderResource(0, gpu_texture);
        gpu_context->SetDrawTopology(DRAW_TOPOLOGY_TRIANGLE_STRIP);
        gpu_context->Draw(0, 3);
        
        gpu_context->SetFullViewport();
        ImGui::Render();

        gpu_context->PresentOutputBuffer(GPU_PRESENT_BEHAVIOUR_IMMEDIATE);

        needs_redraw = false;
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
        const void* upload_src = pixels;
        uint32 upload_src_stride = row_stride;

        // handle hq upscaling
        switch (hq_scale)
        {
        case 2:
            hq2x_32_rb((uint32_t *)pixels, row_stride, (uint32_t *)hq_texture_buffer, hq_texture_buffer_stride, 160, 144);
            upload_src = hq_texture_buffer;
            upload_src_stride = hq_texture_buffer_stride;
            break;

        case 3:
            hq3x_32_rb((uint32_t *)pixels, row_stride, (uint32_t *)hq_texture_buffer, hq_texture_buffer_stride, 160, 144);
            upload_src = hq_texture_buffer;
            upload_src_stride = hq_texture_buffer_stride;
            break;

        case 4:
            hq4x_32_rb((uint32_t *)pixels, row_stride, (uint32_t *)hq_texture_buffer, hq_texture_buffer_stride, 160, 144);
            upload_src = hq_texture_buffer;
            upload_src_stride = hq_texture_buffer_stride;
            break;
        }

        // write to gpu texture
        gpu_context->WriteTexture(gpu_texture, upload_src, upload_src_stride, upload_src_stride * gpu_texture_height, 0, 0, 0, gpu_texture_width, gpu_texture_height);
        needs_redraw = true;
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

static bool LoadBIOS(State *state, SYSTEM_MODE mode)
{
    struct BiosDesc { const char *filename; uint32 expected_size; };
    static const BiosDesc bios_desc[NUM_SYSTEM_MODES] =
    {
        { "dmg.bin",    256     },
        { "sgb.bin",    256     },
        { "cgb.bin",    2048    }
    };

    SmallString bios_path;
    bios_path.Format("bootroms/%s", bios_desc[mode].filename);
    FileSystem::BuildOSPath(bios_path);

    AutoReleasePtr<ByteStream> pStream = FileSystem::OpenFile(bios_path, BYTESTREAM_OPEN_READ | BYTESTREAM_OPEN_STREAMED);
    if (pStream == nullptr)
    {
        Log_WarningPrintf("Failed to find bios file '%s'", bios_path.GetCharArray());
        return false;
    }

    uint32 actual_size = (uint32)pStream->GetSize();
    uint32 expected_size = bios_desc[mode].expected_size;
    if (actual_size != expected_size)
    {
        Log_ErrorPrintf("Bios file '%s' is incorrect length (expected %u bytes, actual %u bytes)", bios_path.GetCharArray(), expected_size, actual_size);
        return false;
    }

    byte *bios = new byte[actual_size];
    if (!pStream->Read2(bios, actual_size))
    {
        Log_ErrorPrintf("Failed to read bios file '%s'", bios_path.GetCharArray());
        delete[] bios;
        return false;
    }

    Log_InfoPrintf("Loaded bios file '%s' (%u bytes).", bios_path.GetCharArray(), actual_size);
    state->bios = bios;
    state->bios_length = actual_size;
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
    out_args->permissive_memory = false;
    out_args->accurate_timing = true;
    out_args->frame_limiter = true;
    out_args->enable_audio = true;

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
        else if (CHECK_ARG("-framelimiter"))
        {
            out_args->frame_limiter = true;
        }
        else if (CHECK_ARG("-noframelimiter"))
        {
            out_args->frame_limiter = false;
        }
        else if (CHECK_ARG("-accuratetiming"))
        {
            out_args->accurate_timing = true;
        }
        else if (CHECK_ARG("-noaccuratetiming"))
        {
            out_args->accurate_timing = false;
        }
        else if (CHECK_ARG("-audio"))
        {
            out_args->enable_audio = true;
        }
        else if (CHECK_ARG("-noaudio"))
        {
            out_args->enable_audio = false;
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

static bool CompileShaderPrograms(State *state)
{
    static const char* vertex_shader = R"(
        void main(in uint vertex_id : SV_VertexID,
                  out float2 uv : TEXCOORD,
                  out float4 pos : SV_Position)
        {
            float x = float((vertex_id & 2) << 1) - 1.0f;
            float y = 1.0f - float((vertex_id & 1) << 2);
            uv.x = (x + 1.0f) * 0.5f;
            uv.y = 1.0f - ((y + 1.0f) * 0.5f);
            pos = float4(x, y, 0.0f, 1.0f);
        }
    )";

    static const char* pixel_shader = R"(
        Texture2D tex : register(t0);
        SamplerState tex_SamplerState : register(s0);
        void main(in float2 uv : TEXCOORD,
                  out float4 ocol : SV_Target)
        {
            ocol = tex.Sample(tex_SamplerState, uv);
        }
    )";

    AutoReleasePtr<ShaderCompiler> shader_compiler = ShaderCompiler::Create();
    shader_compiler->SetStageSourceCode(SHADER_PROGRAM_STAGE_VERTEX_SHADER, "", vertex_shader, "main");
    shader_compiler->SetStageSourceCode(SHADER_PROGRAM_STAGE_PIXEL_SHADER, "", pixel_shader, "main");

    AutoReleasePtr<ByteStream> shader_blob = ByteStream_CreateGrowableMemoryStream();
    if (!shader_compiler->CompileSingleTypeProgram(state->gpu_device->GetShaderProgramType(), 0, shader_blob, nullptr, nullptr))
    {
        Log_ErrorPrintf("Failed to compile program");
        return false;
    }

    shader_blob->SeekAbsolute(0);
    state->gpu_program = state->gpu_device->CreateGraphicsProgram(shader_blob);
    if (state->gpu_program == nullptr)
    {
        Log_ErrorPrintf("Failed to create program");
        return false;
    }

    return true;
}

static bool InitializeState(const ProgramArgs *args, State *state)
{
    state->bios = nullptr;
    state->bios_length = 0;
    state->cart = nullptr;
    state->system = nullptr;
    state->gpu_device = nullptr;
    state->gpu_context = nullptr;
    state->gpu_texture = nullptr;
    state->gpu_program = nullptr;
    state->window = nullptr;
    state->gpu_texture_width = 0;
    state->gpu_texture_height = 0;
    state->hq_texture_buffer = nullptr;
    state->hq_texture_buffer_stride = 0;
    state->hq_scale = 0;
    state->audio_device_id = 0;
    state->running = true;
    state->needs_redraw = false;
    state->show_info_window = false;

    // load cart
    state->system = new System(state);
    if (args->cart_filename != nullptr && !LoadCart(args->cart_filename, state))
        return false;

    // create display
    RendererInitializationParameters parameters;
    parameters.ImplicitSwapChainCaption = "gbe";
    parameters.ImplicitSwapChainWidth = Display::SCREEN_WIDTH * 4;
    parameters.ImplicitSwapChainHeight = Display::SCREEN_HEIGHT * 4;
    if (!RenderLib::CreateRenderDeviceAndWindow(&parameters, &state->gpu_device, &state->gpu_context, &state->window))
        return false;

    // create program
    if (!CompileShaderPrograms(state))
        return false;

    // create texture
    state->SetHQScale(1);
    if (!state->gpu_texture)
        return false;

    // init imgui
    if (!ImGuiBridge::Initialize(state->gpu_device, state->gpu_context))
        return false;

    // create audio device
    SDL_AudioSpec audio_spec = { 44100, AUDIO_S16, 2, 0, 2048, 0, 0, &State::AudioCallback, (void *)state };
    SDL_AudioSpec obtained_audio_spec;
    state->audio_device_id = SDL_OpenAudioDevice(nullptr, 0, &audio_spec, &obtained_audio_spec, 0);
    if (state->audio_device_id == 0)
        Log_WarningPrintf("Failed to open audio device (error: %s). No audio will be heard.", SDL_GetError());

    // get system mode
    SYSTEM_MODE system_mode = (state->cart != nullptr) ? state->cart->GetSystemMode() : SYSTEM_MODE_DMG;
    Log_InfoPrintf("Using system mode %s.", NameTable_GetNameString(NameTables::SystemMode, system_mode));

    // load bios
    if (!args->disable_bios)
        LoadBIOS(state, system_mode);

    // init system
    if (!state->system->Init(system_mode, state->bios, state->bios_length, state->cart))
    {
        Log_ErrorPrintf("Failed to initialize system");
        return false;
    }

    // apply options
    state->system->SetPermissiveMemoryAccess(args->permissive_memory);
    state->system->SetAccurateTiming(args->accurate_timing);
    state->system->SetAudioEnabled(args->enable_audio);
    state->system->SetFrameLimiter(args->frame_limiter);
    return true;
}

static void CleanupState(State *state)
{
    delete[] state->bios;
    delete state->cart;
    delete state->system;

    delete[] state->hq_texture_buffer;
    state->hq_texture_buffer = nullptr;

    ImGuiBridge::Shutdown();
    SAFE_RELEASE(state->gpu_texture);
    SAFE_RELEASE(state->gpu_program);
    SAFE_RELEASE(state->gpu_context);
    SAFE_RELEASE(state->window);
    SAFE_RELEASE(state->gpu_device);

    if (state->audio_device_id != 0)
        SDL_CloseAudioDevice(state->audio_device_id);
}


static int Run(State *state)
{
    Timer time_since_last_report;

    // resume audio
    if (state->audio_device_id != 0)
        SDL_PauseAudioDevice(state->audio_device_id, 0);

    // initial frame
    ImGuiBridge::NewFrame();

    // main loop
    while (state->running)
    {
        SDL_PumpEvents();

        if (state->window->HandleMessages(state->gpu_context))
        {
            ImGuiBridge::SetDisplaySize(state->window->GetWidth(), state->window->GetHeight());
            state->needs_redraw = true;
        }

        for (;;)
        {
            SDL_Event events[16];
            int nevents = SDL_PeepEvents(events, countof(events), SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (nevents == 0)
                break;

            for (int i = 0; i < nevents; i++)
            {
                const SDL_Event *event = events + i;

                if (ImGuiBridge::HandleSDLEvent(event, false))
                    continue;

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
                                    state->SetHQScale((event->key.keysym.sym - SDLK_1) + 1);

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

        // report statistics (done first so to not interfere with sleep time calc)
        if (time_since_last_report.GetTimeSeconds() >= 1.0)
        {
            state->system->CalculateCurrentSpeed();
            Log_InfoPrintf("Current frame: %u, emulation speed: %.3f%% (%.2f FPS), target emulation speed: %.3f%%", state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f, state->system->GetCurrentFPS(), state->system->GetTargetSpeed() * 100.0f);
            time_since_last_report.Reset();

            // update window title
            SmallString window_title;
            window_title.Format("gbe - %s - Frame %u - %.0f%% (%.2f FPS)", (state->cart != nullptr) ? state->cart->GetName().GetCharArray() : "NO CARTRIDGE", state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f, state->system->GetCurrentFPS());
            state->window->SetWindowTitle(window_title);
        }

        // run a frame
        double sleep_time_seconds = state->system->ExecuteFrame();

        // needs redraw?
        if (state->needs_redraw)
        {
            state->DrawImGui();
            state->Redraw();
            ImGuiBridge::NewFrame();
        }

        // sleep until the next frame
        uint32 sleep_time_ms = (uint32)std::floor(sleep_time_seconds * 1000.0);
        if (sleep_time_ms > 0)
            Thread::Sleep(sleep_time_ms);
    }

    // pause audio
    if (state->audio_device_id != 0)
        SDL_PauseAudioDevice(state->audio_device_id, 1);

    return 0;
}

// SDL requires the entry point declared without c++ decoration
extern "C" int main(int argc, char *argv[])
{
#ifdef Y_BUILD_CONFIG_DEBUG
    g_pLog->SetConsoleOutputParams(true, nullptr, LOGLEVEL_PROFILE);
#else
    g_pLog->SetConsoleOutputParams(true, nullptr, LOGLEVEL_INFO);
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

    // hqx init
    hqxInit();

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
