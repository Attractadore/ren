#include "ImGuiApp.hpp"
#include "ren/ren-imgui.hpp"

#include <algorithm>
#include <imgui_impl_sdl2.h>

ImGuiApp::ImGuiApp(const char *name) : AppBase(name) {
  [&]() -> Result<void> {
    if (!IMGUI_CHECKVERSION()) {
      bail("ImGui: failed to check version");
    }

    m_imgui_context.reset(ImGui::CreateContext());
    if (!m_imgui_context) {
      bail("ImGui: failed to create context");
    }

    ImGui::StyleColorsDark();

    float dpi;
    {
      int display = SDL_GetWindowDisplayIndex(get_window());
      if (display < 0) {
        bail("SDL2: failed to get window display: ", SDL_GetError());
      }
      if (SDL_GetDisplayDPI(display, &dpi, nullptr, nullptr)) {
        bail("SDL2: failed to get DPI: ", SDL_GetError());
      }
    }

    // Scale ImGui UI based on DPI
    // A 15.6 inch 1920x1080 display's DPI is 142
    float ui_scale = dpi / 142.0f;

    ImGuiIO &io = ImGui::GetIO();
    ImFont *default_font = io.Fonts->AddFontDefault();
    ImFontConfig font_config = *std::ranges::find_if(
        io.Fonts->ConfigData, [&](const ImFontConfig &font_config) {
          return font_config.DstFont == default_font;
        });
    font_config.FontDataOwnedByAtlas = false;
    font_config.SizePixels = glm::floor(16.0f * ui_scale);
    std::ranges::fill(font_config.Name, '\0');
    font_config.DstFont = nullptr;
    m_font = io.Fonts->AddFont(&font_config);
    io.Fonts->Build();

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(ui_scale);

    if (!ImGui_ImplSDL2_InitForVulkan(get_window())) {
      bail("ImGui-SDL2: failed to init backend");
    }

    ren::imgui::set_context(get_scene(), m_imgui_context.get());
    return {};
  }()
               .transform_error(throw_error);
}

void ImGuiApp::Deleter::operator()(ImGuiContext *context) const noexcept {
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
  ImGui::PushFont(m_font);
  return {};
}

auto ImGuiApp::end_frame() -> Result<void> {
  ren::imgui::draw(get_scene());
  ImGui::PopFont();
  if (m_imgui_enabled) {
    ImGui::Render();
  }
  ImGui::EndFrame();
  return {};
}

auto ImGuiApp::imgui_wants_capture_keyboard() const -> bool {
  return m_imgui_enabled and ImGui::GetIO().WantCaptureKeyboard;
}

auto ImGuiApp::imgui_wants_capture_mouse() const -> bool {
  return m_imgui_enabled and ImGui::GetIO().WantCaptureMouse;
}
