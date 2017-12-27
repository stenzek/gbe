// ImGui SDL2 binding with OpenGL3
// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(),
// ImGui::Render() and ImGui_ImplXXXX_Shutdown(). If you are new to ImGui, see examples/README.txt and documentation at
// the top of imgui.cpp. https://github.com/ocornut/imgui

#include <SDL.h>

bool ImGui_Impl_Init(SDL_Window* window);
void ImGui_Impl_Shutdown();
void ImGui_Impl_NewFrame();
bool ImGui_Impl_ProcessEvent(const SDL_Event* event);
void ImGui_Impl_RenderOSD();

// Use if you want to reset your rendering device without losing ImGui state.
void ImGui_Impl_InvalidateDeviceObjects();
bool ImGui_Impl_CreateDeviceObjects();
