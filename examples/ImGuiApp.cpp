#include "ImGuiApp.hpp"
#include "ren/core/Algorithm.hpp"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <imgui.hpp>

void ImGuiApp::init(ren::String8 name) {
  AppBase::init(name);

  if (!IMGUI_CHECKVERSION()) {
    fmt::println(stderr, "ImGui: failed to check version");
    exit(EXIT_FAILURE);
  }

  if (!ImGui::CreateContext()) {
    fmt::println(stderr, "ImGui: failed to create context");
    exit(EXIT_FAILURE);
  }

  ImGui::StyleColorsDark();

  SDL_Window *window = get_window();
  float display_scale = SDL_GetWindowDisplayScale(window);
  float pixel_density = SDL_GetWindowPixelDensity(window);

  // fmt::println("ImGui UI scale: {}", m_ui_scale);

  ImGuiIO &io = ImGui::GetIO();
  ImFont *default_font = io.Fonts->AddFontDefault();
  ImFontConfig font_config = *find_if(
      ren::Span(io.Fonts->ConfigData), [&](const ImFontConfig &font_config) {
        return font_config.DstFont == default_font;
      });
  font_config.FontDataOwnedByAtlas = false;
  font_config.SizePixels = glm::floor(font_config.SizePixels * display_scale);
  fill(ren::Span(font_config.Name), '\0');
  font_config.DstFont = nullptr;
  m_font = io.Fonts->AddFont(&font_config);
  io.Fonts->Build();
  io.FontGlobalScale = 1.0f / pixel_density;

  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(display_scale / pixel_density);

  if (!ImGui_ImplSDL3_InitForVulkan(get_window())) {
    fmt::println(stderr, "ImGui-SDL3: failed to init backend");
    exit(EXIT_FAILURE);
  }

  ren::init_imgui(&m_frame_arena, get_scene());
}

void ImGuiApp::quit() {
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  AppBase::quit();
}

void ImGuiApp::process_event(const SDL_Event &event) {
  switch (event.type) {
  default: {
    ImGui_ImplSDL3_ProcessEvent(&event);
    break;
  }
  }
  if (not imgui_wants_capture_keyboard() and
      event.type == SDL_EVENT_KEY_DOWN and
      event.key.scancode == SDL_SCANCODE_G) {
    m_imgui_enabled = not m_imgui_enabled;
  }
  AppBase::process_event(event);
}

void ImGuiApp::begin_frame() {
  ImGui_ImplSDL3_NewFrame();
  ImGuiIO &io = ImGui::GetIO();

  ImGui::NewFrame();

  ImGui::PushFont(m_font);

#if 0
  static bool open = true;
  if (open) {
    ImGui::ShowDemoWindow(&open);
  }
#endif

  ImGui::Begin("ImGuiApp", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
  ImGui::SetWindowPos({0.0f, 0.0f});
  ImGui::SetWindowSize({io.DisplaySize.x * 0.3f, io.DisplaySize.y});

  if (ImGui::CollapsingHeader("Renderer settings")) {
    ren::draw_imgui(get_scene());
  }
}

void ImGuiApp::end_frame() {
  ImGui::End();
  ImGui::PopFont();
  if (m_imgui_enabled) {
    ImGui::Render();
  }
  ImGui::EndFrame();
}

auto ImGuiApp::imgui_wants_capture_keyboard() const -> bool {
  return m_imgui_enabled and ImGui::GetIO().WantCaptureKeyboard;
}

auto ImGuiApp::imgui_wants_capture_mouse() const -> bool {
  return m_imgui_enabled and ImGui::GetIO().WantCaptureMouse;
}
