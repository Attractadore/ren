#include "ImGuiApp.hpp"
#include "ren/ren-imgui.hpp"

#include <imgui_impl_sdl2.h>

ImGuiApp::ImGuiApp(const char *name) : AppBase(name) {
  [&] -> Result<void> {
    if (!IMGUI_CHECKVERSION()) {
      bail("ImGui: failed to check version");
    }
    m_imgui_context.reset(ImGui::CreateContext());
    if (!m_imgui_context) {
      bail("ImGui: failed to create context");
    }
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL2_InitForVulkan(get_window())) {
      bail("ImGui-SDL2: failed to init backend");
    }
    ren::imgui::set_context(get_scene(), m_imgui_context.get());
    return {};
  }()
             .transform_error(throw_error);
}

void ImGuiApp::Deleter::operator()(ImGuiContext *context) noexcept {
  if (context) {
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context);
  }
}

auto ImGuiApp::process_event(const SDL_Event &event) -> Result<void> {
  ImGui_ImplSDL2_ProcessEvent(&event);
  if (not imgui_wants_capture_keyboard() and event.type == SDL_KEYDOWN and
      event.key.keysym.scancode == SDL_SCANCODE_G) {
    m_imgui_enabled = not m_imgui_enabled;
    ren::imgui::set_context(get_scene(),
                            m_imgui_enabled ? m_imgui_context.get() : nullptr);
  }
  return {};
}

auto ImGuiApp::begin_frame() -> Result<void> {
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  return {};
}

auto ImGuiApp::end_frame() -> Result<void> {
  ImGui::EndFrame();
  return {};
}

auto ImGuiApp::imgui_wants_capture_keyboard() const -> bool {
  return m_imgui_enabled and ImGui::GetIO().WantCaptureKeyboard;
}

auto ImGuiApp::imgui_wants_capture_mouse() const -> bool {
  return m_imgui_enabled and ImGui::GetIO().WantCaptureMouse;
}
