#include "ImGuiApp.hpp"
#include "ren/ren-imgui.hpp"

#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <imgui_impl_sdl2.h>

// Taken from imgui_impl_sdl2.cpp
struct ImGui_ImplSDL2_Data {
  SDL_Window *Window;
  SDL_Renderer *Renderer;
  Uint64 Time;
  char *ClipboardTextData;

  // Mouse handling
  Uint32 MouseWindowID;
  int MouseButtonsDown;
  SDL_Cursor *MouseCursors[ImGuiMouseCursor_COUNT];
  SDL_Cursor *MouseLastCursor;
  int MouseLastLeaveFrame;
  bool MouseCanUseGlobalState;

  // Gamepad handling
  ImVector<SDL_GameController *> Gamepads;
  ImGui_ImplSDL2_GamepadMode GamepadMode;
  bool WantUpdateGamepadsList;
};

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

    std::string_view drv = SDL_GetCurrentVideoDriver();
    if (drv == "x11") {
      // SDL2 doesn't support HiDPI on X11.
      // TODO: Set UI scale to Xft.dpi / 96.
      m_ui_scale = 2.0f;
    } else {
      // On Win32 and Wayland, set scaling factor to ratio between framebuffer
      // and window size.
      int w, h;
      SDL_GetWindowSize(get_window(), &w, &h);
      int dw, dh;
      SDL_Vulkan_GetDrawableSize(get_window(), &dw, &dh);
      m_ui_scale = (float)dw / (float)w;
      m_mouse_scale = m_ui_scale;
    }

    fmt::println("ImGui UI scale: {}, ImGui mouse scale: {}", m_ui_scale,
                 m_mouse_scale);

    ImGuiIO &io = ImGui::GetIO();
    ImFont *default_font = io.Fonts->AddFontDefault();
    ImFontConfig font_config = *std::ranges::find_if(
        io.Fonts->ConfigData, [&](const ImFontConfig &font_config) {
          return font_config.DstFont == default_font;
        });
    font_config.FontDataOwnedByAtlas = false;
    font_config.SizePixels = glm::floor(font_config.SizePixels * m_ui_scale);
    std::ranges::fill(font_config.Name, '\0');
    font_config.DstFont = nullptr;
    m_font = io.Fonts->AddFont(&font_config);
    io.Fonts->Build();

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(m_ui_scale);

    if (!ImGui_ImplSDL2_InitForVulkan(get_window())) {
      bail("ImGui-SDL2: failed to init backend");
    }

    // FIXME: hack to disable global mouse query on Win32 and X11. So it's
    // possible to process them here.
    auto *bd = (ImGui_ImplSDL2_Data *)ImGui::GetIO().BackendPlatformUserData;
    m_global_mouse_state = bd->MouseCanUseGlobalState;
    bd->MouseCanUseGlobalState = false;

    ren::imgui::set_context(get_scene(), m_imgui_context.get());
    return {};
  }()
               .transform_error(throw_error)
               .value();
}

void ImGuiApp::Deleter::operator()(ImGuiContext *context) const noexcept {
  if (context) {
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context);
  }
}

auto ImGuiApp::process_event(const SDL_Event &event) -> Result<void> {
  switch (event.type) {
  case SDL_MOUSEMOTION: {
    SDL_Event patched = event;
    patched.motion.x *= m_mouse_scale;
    patched.motion.y *= m_mouse_scale;
    patched.motion.xrel *= m_mouse_scale;
    patched.motion.yrel *= m_mouse_scale;
    ImGui_ImplSDL2_ProcessEvent(&patched);
    break;
  }
  default:
    ImGui_ImplSDL2_ProcessEvent(&event);
    break;
  }
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

  ImGuiIO &io = ImGui::GetIO();
  int dw, dh;
  SDL_Vulkan_GetDrawableSize(get_window(), &dw, &dh);
  io.DisplaySize = ImVec2(dw, dh);
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

  const auto *bd =
      (const ImGui_ImplSDL2_Data *)ImGui::GetIO().BackendPlatformUserData;
  if (get_window() == SDL_GetKeyboardFocus() and m_global_mouse_state and
      (bd->MouseButtonsDown == 0)) {
    int window_x, window_y, mouse_x_global, mouse_y_global;
    SDL_GetGlobalMouseState(&mouse_x_global, &mouse_y_global);
    SDL_GetWindowPosition(get_window(), &window_x, &window_y);
    io.AddMousePosEvent((float)(mouse_x_global - window_x) * m_mouse_scale,
                        (float)(mouse_y_global - window_y) * m_mouse_scale);
  }

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
