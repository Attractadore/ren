#include "ren/core/Algorithm.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/ren.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <imgui.hpp>

namespace ren {

struct EditorUi {
  SDL_PropertiesID m_create_project_dialog_properties = 0;
};

struct EditorContext {
  Arena m_arena;
  Arena m_frame_arena;
  Renderer *m_renderer = nullptr;
  SDL_Window *m_window = nullptr;
  SwapChain *m_swap_chain = nullptr;
  Scene *m_scene = nullptr;
  ren::Handle<Camera> m_camera;
  ImFont *m_font = nullptr;
  bool m_quit = false;
  EditorUi m_ui;
};

void init_editor(int argc, const char *argv[], NotNull<EditorContext *> ctx) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fmt::println(stderr, "Failed to init SDL3: {}", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  ScratchArena::init_allocator();

  ctx->m_arena = Arena::init();
  ctx->m_frame_arena = Arena::init();

  {
    u32 adapter = DEFAULT_ADAPTER;
    const char *user_adapter = std::getenv("REN_ADAPTER");
    if (user_adapter) {
      char *end;
      adapter = std::strtol(user_adapter, &end, 10);
      if (end != user_adapter + std::strlen(user_adapter)) {
        adapter = DEFAULT_ADAPTER;
      }
    }

    ctx->m_renderer = create_renderer(&ctx->m_arena, {.adapter = adapter});
    if (!ctx->m_renderer) {
      exit(EXIT_FAILURE);
    }
  }

  ctx->m_window = SDL_CreateWindow(
      "ren editor", 1280, 720,
      SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY |
          SDL_WINDOW_RESIZABLE | get_sdl_window_flags(ctx->m_renderer));
  if (!ctx->m_window) {
    fmt::println(stderr, "Failed to create window: {}", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  ctx->m_swap_chain =
      create_swapchain(&ctx->m_arena, ctx->m_renderer, ctx->m_window);
  if (!ctx->m_swap_chain) {
    exit(EXIT_FAILURE);
  }

  ctx->m_scene =
      create_scene(&ctx->m_arena, ctx->m_renderer, ctx->m_swap_chain);
  if (!ctx->m_scene) {
    exit(EXIT_FAILURE);
  }

  ctx->m_camera = create_camera(ctx->m_scene);
  set_camera(ctx->m_scene, ctx->m_camera);

  if (!IMGUI_CHECKVERSION()) {
    fmt::println(stderr, "Failed to check ImGui version");
    exit(EXIT_FAILURE);
  }

  if (!ImGui::CreateContext()) {
    fmt::println(stderr, "Failed to create ImGui context");
    exit(EXIT_FAILURE);
  }

  ImGui::StyleColorsDark();

  float display_scale = SDL_GetWindowDisplayScale(ctx->m_window);
  float pixel_density = SDL_GetWindowPixelDensity(ctx->m_window);

  ImGuiIO &io = ImGui::GetIO();
  ImFont *default_font = io.Fonts->AddFontDefault();
  ImFontConfig font_config = *find_if(
      Span(io.Fonts->ConfigData), [&](const ImFontConfig &font_config) {
        return font_config.DstFont == default_font;
      });
  font_config.FontDataOwnedByAtlas = false;
  font_config.SizePixels = glm::floor(font_config.SizePixels * display_scale);
  fill(ren::Span(font_config.Name), '\0');
  font_config.DstFont = nullptr;
  ctx->m_font = io.Fonts->AddFont(&font_config);
  io.Fonts->Build();
  io.FontGlobalScale = 1.0f / pixel_density;

  ImGui::GetStyle().ScaleAllSizes(display_scale / pixel_density);

  if (!ImGui_ImplSDL3_InitForVulkan(ctx->m_window)) {
    fmt::println(stderr, "Failed to init ImGui backend");
    exit(EXIT_FAILURE);
  }

  ren::init_imgui(&ctx->m_frame_arena, ctx->m_scene);
};

void SDLCALL new_project_callback(void *userdata, const char *const *filelist,
                                  int filter) {
  auto *ctx = (EditorContext *)userdata;
  if (!filelist) {
    fmt::println(stderr, "Failed to create new project: {}", SDL_GetError());
    return;
  }
  const char *file = *filelist;
  if (!file) {
    return;
  }
  fmt::println("Create new project at {}", file);
}

void draw_editor_ui(NotNull<EditorContext *> ctx) {
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGui::PushFont(ctx->m_font);

#if 0
  bool open = true;
  ImGui::ShowDemoWindow(&open);
#endif
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        SDL_PropertiesID &p = ctx->m_ui.m_create_project_dialog_properties;
        if (!p) {
          p = SDL_CreateProperties();
          SDL_SetPointerProperty(p, SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                                 ctx->m_window);
          SDL_SetBooleanProperty(p, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
          SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                                "New Project");
        }
        ScratchArena scratch;
        Path app_data = app_data_directory(scratch);
        app_data = app_data.concat(scratch, Path::init("ren"));
        app_data = app_data.concat(scratch, Path::init("projects"));
        if (not app_data.exists().value_or(false)) {
          if (IoResult<void> result = create_directories(app_data); !result) {
            fmt::println(stderr, "Failed to create {}: {}", app_data,
                         result.error());
          }
        }
        if (!SDL_GetStringProperty(p, SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                                   nullptr)) {
          const char *location = app_data.m_str.zero_terminated(&ctx->m_arena);
          SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                                location);
        }
        SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER,
                                         new_project_callback, ctx, p);
      }
      if (ImGui::MenuItem("Quit")) {
        ctx->m_quit = true;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  ImGui::PopFont();
  ImGui::Render();
  ImGui::EndFrame();
}

void run_editor(NotNull<EditorContext *> ctx) {
  u64 time = clock();
  while (not ctx->m_quit) {
    u64 now = clock();
    u64 dt_ns = now - time;
    time = now;

    ImGuiIO &io = ImGui::GetIO();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);

      switch (event.type) {
      case SDL_EVENT_MOUSE_MOTION:
      case SDL_EVENT_MOUSE_WHEEL:
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (io.WantCaptureMouse) {
          continue;
        }
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
        if (io.WantCaptureKeyboard) {
          continue;
        }
        break;
      case SDL_EVENT_QUIT:
        ctx->m_quit = true;
        break;
      }
    }

    draw_editor_ui(ctx);
    ren::draw(ctx->m_scene, {.delta_time = dt_ns / 1e9f});
  }
}

void quit_editor(NotNull<EditorContext *> ctx) {
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  destroy_scene(ctx->m_scene);
  destroy_swap_chain(ctx->m_swap_chain);
  SDL_DestroyWindow(ctx->m_window);
  ren::destroy_renderer(ctx->m_renderer);
  SDL_Quit();
}

} // namespace ren

int main(int argc, const char *argv[]) {
  ren::EditorContext ctx;
  ren::init_editor(argc, argv, &ctx);
  ren::run_editor(&ctx);
  ren::quit_editor(&ctx);
}
