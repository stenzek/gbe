#include "YBaseLib/Windows/WindowsHeaders.h"

#include <cstdio>
#include <hqx.h>
#include <glad/glad.h>
#include <SDL.h>
#include <imgui.h>

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

#include "imgui_impl.h"

Log_SetChannel(Main);

struct ProgramArgs
{
    const char *bios_filename;
    const char *cart_filename;
    SYSTEM_MODE system_mode;
    bool disable_bios;
    bool permissive_memory;
    bool accurate_timing;
    bool frame_limiter;
    bool enable_audio;
    bool enable_hqx;
};

struct State : public System::CallbackInterface
{
    Cartridge *cart;
    const byte *bios;
    uint32 bios_length;

    System *system;

    SDL_Window *window;
    SDL_GLContext gl_context;

    GLuint texture;
    GLuint attributeless_vao;
    GLuint display_vertex_shader;
    GLuint display_fragment_shader;
    GLuint display_program;
    
    uint32 gpu_texture_width;
    uint32 gpu_texture_height;
    byte* hq_texture_buffer;
    uint32 hq_texture_buffer_stride;
    uint32 hq_scale;

    SDL_AudioDeviceID audio_device_id;

    String savestate_prefix;

    bool enable_hqx;

    bool running;

    bool needs_redraw;

    bool show_info_window;

    bool vsync_enabled;

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

    void ReallocateGPUTexture(uint32 scale, bool force = true)
    {
        scale = Math::Clamp(scale, 1u, 4u);
        if (hq_scale == scale && !force)
            return;

        if (texture != 0)
            glDeleteTextures(1, &texture);

        gpu_texture_width = Display::SCREEN_WIDTH * scale;
        gpu_texture_height = Display::SCREEN_HEIGHT * scale;

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gpu_texture_width, gpu_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        delete[] hq_texture_buffer;
        hq_texture_buffer = nullptr;
        hq_texture_buffer_stride = 0;
        hq_scale = scale;

        if (hq_scale > 1)
        {
            // only alloc buffer for >1x
            hq_texture_buffer_stride = 4 * gpu_texture_width;
            hq_texture_buffer = new byte[hq_texture_buffer_stride * gpu_texture_height];
        }

        // resize output window?
        //SDL_SetWindowSize(window, gpu_texture_width, gpu_texture_height);
    }

    void DrawImGui()
    {
        static bool link_client_window = false;

        bool boolOption;

        ImGui_Impl_RenderOSD();

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
                    ReallocateGPUTexture(1);

                if (ImGui::MenuItem("2x", nullptr, (hq_scale == 2)))
                    ReallocateGPUTexture(2);

                if (ImGui::MenuItem("3x", nullptr, (hq_scale == 3)))
                    ReallocateGPUTexture(3);

                if (ImGui::MenuItem("4x", nullptr, (hq_scale == 4)))
                    ReallocateGPUTexture(4);

                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Host Link Server"))
            {
                Log_InfoPrintf("Hosting link server.");

                Error error;
                if (!LinkConnectionManager::GetInstance().Host("0.0.0.0", 1337, &error))
                    Log_ErrorPrintf("  Failed: %s", error.GetErrorCodeAndDescription().GetCharArray());
            }

            if (ImGui::MenuItem("Connect Link Client"))
                link_client_window = true;

            ImGui::Separator();

            if (ImGui::MenuItem("Quit"))
                running = false;

            ImGui::EndPopup();
        }

        if (show_info_window)
        {
            ImGui::SetNextWindowPos(ImVec2(4.0f, 4.0f), ImGuiSetCond_FirstUseEver);

            if (ImGui::Begin("Info Window", &show_info_window, ImVec2(148.0f, 48.0f), 0.5f, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::Text("Frame %u (%.0f%%)", system->GetFrameCounter() + 1, system->GetCurrentSpeed() * 100.0f);
                ImGui::Text("%.2f FPS", system->GetCurrentFPS());
                ImGui::End();
            }
        }

        if (link_client_window && ImGui::Begin("Connect Link Client", &link_client_window))
        {
            char host[100] = "127.0.0.1";
            ImGui::InputText("Host", host, sizeof(host));
            if (ImGui::Button("Connect"))
            {
                Log_InfoPrintf("Connecting to link server...");
                system->SetPaused(true);

                Error error;
                if (!LinkConnectionManager::GetInstance().Connect(host, 1337, &error))
                    Log_ErrorPrintf("  Failed: %s", error.GetErrorCodeAndDescription().GetCharArray());

                system->SetPaused(false);
                link_client_window = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                link_client_window = false;

            ImGui::End();
        }
    }

    void Redraw()
    {
        int window_width, window_height;
        SDL_GetWindowSize(window, &window_width, &window_height);
        glViewport(0, 0, window_width, window_height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        uint32 width = static_cast<uint32>(window_width);
        uint32 height = static_cast<uint32>(window_height);
        uint32 vp_width;
        uint32 vp_height;
        if ((width * Display::SCREEN_HEIGHT / Display::SCREEN_WIDTH) > height)
        {
            // same AR as gameboy, add borders on left/right
            vp_width = height * Display::SCREEN_WIDTH / Display::SCREEN_HEIGHT;
            vp_height = height;
        }
        else
        {
            // draw borders on top/bottom
            vp_width = width;
            vp_height = width * Display::SCREEN_HEIGHT / Display::SCREEN_WIDTH;
        }
        glViewport(static_cast<int>((width - vp_width) / 2),
                   static_cast<int>((height - vp_height) / 2),
                   vp_width, vp_height);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
        glDisable(GL_SCISSOR_TEST);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glUseProgram(display_program);
        glBindVertexArray(attributeless_vao);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        ImGui::Render();

        bool new_vsync_state = system->GetFrameLimiter();
        if (new_vsync_state != vsync_enabled)
        {
            SDL_GL_SetSwapInterval(new_vsync_state ? 1 : 0);
            vsync_enabled = new_vsync_state;
        }

        SDL_GL_SwapWindow(window);

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
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gpu_texture_width, gpu_texture_height, GL_RGBA, GL_UNSIGNED_BYTE, upload_src);
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

        if (pStream->GetSize() < (uint64)expected_data_size)
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

    SmallString program_file_name;
    SmallString bios_path;
    Platform::GetProgramFileName(program_file_name);
    FileSystem::BuildPathRelativeToFile(bios_path, program_file_name, SmallString::FromFormat("bootroms/%s", bios_desc[mode].filename));

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
    out_args->system_mode = NUM_SYSTEM_MODES;
    out_args->disable_bios = false;
    out_args->permissive_memory = false;
    out_args->accurate_timing = true;
    out_args->frame_limiter = true;
    out_args->enable_audio = true;
    out_args->enable_hqx = false;

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
        else if (CHECK_ARG_PARAM("-mode"))
        {
            i++;
            if (!Y_stricmp(argv[i], "auto"))
                out_args->system_mode = SYSTEM_MODE_SGB;
            else if (!Y_stricmp(argv[i], "dmg"))
                out_args->system_mode = SYSTEM_MODE_DMG;
            else if (!Y_stricmp(argv[i], "sgb"))
                out_args->system_mode = SYSTEM_MODE_SGB;
            else if (!Y_stricmp(argv[i], "cgb"))
                out_args->system_mode = SYSTEM_MODE_CGB;
            else
            {
                fprintf(stderr, "Unknown system mode: '%s'", argv[i]);
                return false;
            }
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
        else if (CHECK_ARG("-hqx"))
        {
            out_args->enable_hqx = true;
        }
        else if (CHECK_ARG("-nohqx"))
        {
            out_args->enable_hqx = false;
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

static GLuint CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0)
        return 0;

    int source_length = static_cast<int>(Y_strlen(source));
    glShaderSource(shader, 1, &source, &source_length);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        String info_log;
        info_log.Resize(static_cast<uint32>(length));
        GLsizei actual_length = length;
        glGetShaderInfoLog(shader, length, &actual_length, info_log.GetWriteableCharArray());
        info_log.Resize(static_cast<uint32>(actual_length));

        Log_ErrorPrintf("Shader compile error: %s", info_log.GetCharArray());
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static bool LinkProgram(GLuint program)
{
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        String info_log;
        info_log.Resize(static_cast<uint32>(length));
        GLsizei actual_length = length;
        glGetProgramInfoLog(program, length, &actual_length, info_log.GetWriteableCharArray());
        info_log.Resize(static_cast<uint32>(actual_length));

        Log_ErrorPrintf("Program link error: %s", info_log.GetCharArray());
        return false;
    }

    return true;
}

static bool CompileShaderPrograms(State *state)
{
    static const char* vertex_shader = R"(
        #version 330
        out vec2 uv0;
        void main()
        {
            vec2 pos = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
            uv0 = pos;
            gl_Position = vec4(pos * 2.0f - 1.0f, 0.0f, 1.0f);
            gl_Position.y = -gl_Position.y;
        }
    )";

    static const char* pixel_shader = R"(
        #version 330
        uniform sampler2D samp0;
        in vec2 uv0;
        out vec4 ocol0;
        void main()
        {
            ocol0 = vec4(texture(samp0, uv0).xyz, 1.0);
            //ocol0 = vec4(0.2f, 0.4f, 0.6f, 1.0f);
            //ocol0 = vec4(uv0, 0.2f, 1.0f);
        }
    )";

    state->display_vertex_shader = CompileShader(GL_VERTEX_SHADER, vertex_shader);
    state->display_fragment_shader = CompileShader(GL_FRAGMENT_SHADER, pixel_shader);

    state->display_program = glCreateProgram();
    if (state->display_program == 0)
        return false;

    glAttachShader(state->display_program, state->display_vertex_shader);
    glAttachShader(state->display_program, state->display_fragment_shader);
    glBindFragDataLocation(state->display_program, 0, "ocol0");
    if (!LinkProgram(state->display_program))
        return false;

    glUseProgram(state->display_program);
    GLint location = glGetUniformLocation(state->display_program, "samp0");
    if (location >= 0)
        glUniform1i(location, 0);
    glUseProgram(0);

    glGenVertexArrays(1, &state->attributeless_vao);
    if (state->attributeless_vao == 0)
        return false;

    return true;
}

static int GetGLContextFlags()
{
    int flags = SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;

#ifdef Y_BUILD_CONFIG_DEBUG
    flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
#endif

    return flags;
}

#ifdef Y_BUILD_CONFIG_DEBUG

static void APIENTRY GLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *user_param)
{
    if (severity == GL_DEBUG_SEVERITY_HIGH_KHR)
        Log_ErrorPrintf("GL high: %s", message);
    else if (severity == GL_DEBUG_SEVERITY_MEDIUM_KHR)
        Log_WarningPrintf("GL medium: %s", message);
    else
        Log_DevPrintf("GL low: %s", message);
}

#endif

static bool InitializeState(const ProgramArgs *args, State *state)
{
    state->bios = nullptr;
    state->bios_length = 0;
    state->cart = nullptr;
    state->system = nullptr;
    state->texture = 0;
    state->display_vertex_shader = 0;
    state->display_fragment_shader = 0;
    state->display_program = 0;
    state->window = nullptr;
    state->gpu_texture_width = 0;
    state->gpu_texture_height = 0;
    state->hq_texture_buffer = nullptr;
    state->hq_texture_buffer_stride = 0;
    state->hq_scale = 0;
    state->audio_device_id = 0;
    state->enable_hqx = args->enable_hqx;
    state->running = true;
    state->needs_redraw = false;
    state->show_info_window = false;
    state->vsync_enabled = false;

    // load cart
    state->system = new System(state);
    if (args->cart_filename != nullptr && !LoadCart(args->cart_filename, state))
        return false;

    // create display
    state->window = SDL_CreateWindow("gbe",
                                     SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     Display::SCREEN_WIDTH * 2, Display::SCREEN_HEIGHT * 2,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!state->window)
        return false;

    // create GL context
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, GL_TRUE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, GetGLContextFlags());
    state->gl_context = SDL_GL_CreateContext(state->window);
    if (!state->gl_context)
        return false;

    if (SDL_GL_SetSwapInterval(0) != 0)
        Log_WarningPrintf("Failed to clear vsync setting.");

    if (!gladLoadGL())
        return false;

#ifdef Y_BUILD_CONFIG_DEBUG
    if (GLAD_GL_KHR_debug)
    {
        glDebugMessageCallbackKHR(GLDebugCallback, nullptr);
        glEnable(GL_DEBUG_OUTPUT_KHR);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
    }
#endif

    // create program
    if (!CompileShaderPrograms(state))
        return false;

    // create texture
    state->ReallocateGPUTexture(1);
    if (!state->texture)
        return false;

    // init imgui
    ImGui::GetIO().IniFilename = nullptr;
    if (!ImGui_Impl_Init(state->window))
        return false;

    // create audio device
    SDL_AudioSpec audio_spec = { 44100, AUDIO_S16, 2, 0, 2048, 0, 0, &State::AudioCallback, (void *)state };
    SDL_AudioSpec obtained_audio_spec;
    state->audio_device_id = SDL_OpenAudioDevice(nullptr, 0, &audio_spec, &obtained_audio_spec, 0);
    if (state->audio_device_id == 0)
        Log_WarningPrintf("Failed to open audio device (error: %s). No audio will be heard.", SDL_GetError());

    // get system mode
    SYSTEM_MODE system_mode = (state->cart != nullptr) ? state->cart->GetSystemMode() : SYSTEM_MODE_DMG;
    if (args->system_mode != NUM_SYSTEM_MODES)
    {
        system_mode = args->system_mode;
        Log_WarningPrintf("Forcing system mode %s.", NameTable_GetNameString(NameTables::SystemMode, system_mode));
    }
    else
    {
        Log_InfoPrintf("Using system mode %s.", NameTable_GetNameString(NameTables::SystemMode, system_mode));
    }

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

    ImGui_Impl_Shutdown();
    glDeleteTextures(1, &state->texture);

    SDL_GL_MakeCurrent(nullptr, nullptr);
    SDL_DestroyWindow(state->window);

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
    ImGui_Impl_NewFrame();

    // main loop
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

                if (ImGui_Impl_ProcessEvent(event))
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

                        case SDLK_RSHIFT:
                            state->system->SetPadButton(PAD_BUTTON_SELECT, down);
                            break;

                        case SDLK_RETURN:
                            state->system->SetPadButton(PAD_BUTTON_START, down);
                            break;

                        case SDLK_TAB:
                            {
                                bool new_state = !down;
                                if (state->system->GetFrameLimiter() != new_state)
                                    state->system->SetFrameLimiter(new_state);
                            }
                            break;

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
            //Log_InfoPrintf("Current frame: %u, emulation speed: %.3f%% (%.2f FPS), target emulation speed: %.3f%%", state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f, state->system->GetCurrentFPS(), state->system->GetTargetSpeed() * 100.0f);
            time_since_last_report.Reset();

            // update window title
            SmallString window_title;
            window_title.Format("gbe - %s - Frame %u - %.0f%% (%.2f FPS)", (state->cart != nullptr) ? state->cart->GetName().GetCharArray() : "NO CARTRIDGE", state->system->GetFrameCounter() + 1, state->system->GetCurrentSpeed() * 100.0f, state->system->GetCurrentFPS());
            SDL_SetWindowTitle(state->window, window_title);
        }

        // run a frame
        double sleep_time_seconds = state->system->ExecuteFrame();

        // needs redraw?
        if (state->needs_redraw)
        {
            state->DrawImGui();
            state->Redraw();
            ImGui_Impl_NewFrame();
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
