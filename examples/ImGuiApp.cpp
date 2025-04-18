#include "ImGuiApp.hpp"
#include "ren/ren-imgui.hpp"

#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <imgui_impl_sdl2.h>
#ifdef SDL_VIDEO_DRIVER_X11
#include <X11/Xresource.h>
#include <dlfcn.h>
#endif

// Taken from imgui_impl_sdl2.cpp
struct ImGui_ImplSDL2_Data {
  SDL_Window *Window;
  Uint32 WindowID;
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
static_assert(IMGUI_VERSION_NUM == 19150);

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

    SDL_SysWMinfo sys_wm_info = {};
    SDL_VERSION(&sys_wm_info.version);
    bool result = SDL_GetWindowWMInfo(get_window(), &sys_wm_info);
    if (!result) {
      bail("SDL2: failed to get WM info: {}", SDL_GetError());
    }

    switch (sys_wm_info.subsystem) {
    case SDL_SYSWM_X11: {
#ifdef SDL_VIDEO_DRIVER_X11
      // SDL2 doesn't support HiDPI on X11. Set UI scale to Xft.dpi / 96.

      const char *xlib = "libX11.so";
      void *xlib_so = SDL_LoadObject(xlib);
      if (!xlib_so) {
        bail("SDL2: failed to load {}", xlib);
      }

#define load_function(name)                                                    \
  auto *pfn##name = (decltype(name) *)SDL_LoadFunction(xlib_so, #name);        \
  if (!pfn##name) {                                                            \
    bail("SDL2: failed to load {}", #name);                                    \
  }

      load_function(XResourceManagerString);
      load_function(XrmGetStringDatabase);
      load_function(XrmGetResource);

#undef load_function

      const char *resource_string =
          pfnXResourceManagerString(sys_wm_info.info.x11.display);
      XrmDatabase db = pfnXrmGetStringDatabase(resource_string);
      char *type;
      XrmValue value;
      if (pfnXrmGetResource(db, "Xft.dpi", "String", &type, &value) &&
          value.addr) {
        char *end;
        float dpi = std::strtof(value.addr, &end);
        if (end == value.addr + std::strlen(value.addr)) {
          m_ui_scale = dpi / 96.0f;
        }
      }

      SDL_UnloadObject(xlib_so);
#endif
    } break;
    default: {
      // On Win32 and Wayland, set scaling factor to ratio between framebuffer
      // and window size.
      int w, h;
      SDL_GetWindowSize(get_window(), &w, &h);
      int dw, dh;
      SDL_Vulkan_GetDrawableSize(get_window(), &dw, &dh);
      m_ui_scale = (float)dw / (float)w;
      m_mouse_scale = m_ui_scale;
    } break;
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

    // FIXME: hack to disable global mouse query on Win32 and X11.
    auto *bd = (ImGui_ImplSDL2_Data *)ImGui::GetIO().BackendPlatformUserData;
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
  default: {
    ImGui_ImplSDL2_ProcessEvent(&event);
    break;
  }
  }
  if (not imgui_wants_capture_keyboard() and event.type == SDL_KEYDOWN and
      event.key.keysym.scancode == SDL_SCANCODE_G) {
    m_imgui_enabled = not m_imgui_enabled;
    ren::imgui::set_context(get_scene(),
                            m_imgui_enabled ? m_imgui_context.get() : nullptr);
  }
  return AppBase::process_event(event);
}

auto ImGuiApp::begin_frame() -> Result<void> {
  ImGui_ImplSDL2_NewFrame();

  ImGuiIO &io = ImGui::GetIO();
  int dw, dh;
  SDL_Vulkan_GetDrawableSize(get_window(), &dw, &dh);
  io.DisplaySize = ImVec2(dw, dh);
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

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
