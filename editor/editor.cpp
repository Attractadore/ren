#include "ren/core/Algorithm.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/ren.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <imgui.hpp>

namespace ren {

namespace {

struct InputTextCallback_UserData {
  Arena *arena = nullptr;
  DynamicArray<char> *buf = nullptr;
};

bool InputText(const char *label, NotNull<Arena *> arena,
               NotNull<DynamicArray<char> *> buf, ImGuiInputFlags flags = 0) {
  flags |= ImGuiInputTextFlags_CallbackResize;
  InputTextCallback_UserData user_data = {.arena = arena, .buf = buf};
  return ImGui::InputText(
      label, buf->m_data, buf->m_capacity, flags,
      [](ImGuiInputTextCallbackData *data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
          auto [arena, buf] = *(InputTextCallback_UserData *)data->UserData;
          while (buf->m_size < data->BufTextLen + 1) {
            buf->push(arena, 0);
          }
          buf->m_size = data->BufTextLen + 1;
          (*buf)[data->BufTextLen] = 0;
          data->Buf = buf->m_data;
        }
        return 0;
      },
      &user_data);
}

} // namespace

struct NewProjectUi {
  Arena m_arena;

  Arena m_dialog_arena;
  SDL_PropertiesID m_dialog_properties = 0;
  bool m_dialog_active = false;
  alignas(std::atomic<bool>) bool m_dialog_done = false;
  Path m_dialog_path;

  DynamicArray<char> m_title_buffer;
  DynamicArray<char> m_location_buffer;
};

struct EditorUi {
  NewProjectUi m_new_project;
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

void SDLCALL new_project_dialog_callback(void *userdata,
                                         const char *const *filelist,
                                         int filter) {
  auto *ui = (NewProjectUi *)userdata;
  if (!filelist) {
    fmt::println(stderr, "Failed to select new project location: {}",
                 SDL_GetError());
  } else {
    const char *file = *filelist;
    if (file) {
      ui->m_dialog_path = Path::init(&ui->m_dialog_arena, String8::init(file));
    }
  }
  std::atomic_ref(ui->m_dialog_done).store(true, std::memory_order_release);
}

bool new_project(Path path, Path title) {
  ScratchArena scratch;
  Path project_file = title.replace_extension(scratch, Path::init(".json"));
  project_file = path.concat(scratch, project_file);
  if (IoResult<void> result = create_directories(path); !result) {
    fmt::println(stderr, "Failed to create directory {}: {}", path,
                 result.error());
    return false;
  }
  const char buffer[] = {'{', '\n', '}', '\n'};
  if (IoResult<void> result = write(project_file, Span(buffer)); !result) {
    fmt::println(stderr, "Failed to create {}: {}", project_file,
                 result.error());
    return false;
  }
  return true;
}

void draw_editor_ui(NotNull<EditorContext *> ctx) {
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGui::PushFont(ctx->m_font);

  bool open_new_project_popup = false;

#if 0
  bool open = true;
  ImGui::ShowDemoWindow(&open);
#endif
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New...")) {
        open_new_project_popup = true;
      }

      if (ImGui::MenuItem("Quit")) {
        ctx->m_quit = true;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  if (open_new_project_popup) {
    ImGui::OpenPopup("New Project");
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("New Project", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    NewProjectUi &ui = ctx->m_ui.m_new_project;
    if (ImGui::IsWindowAppearing()) {
      if (!ui.m_arena) {
        ui.m_arena = Arena::init();
      }

      const char DEFAULT_TITLE[] = "New Project";
      ui.m_title_buffer = {};
      ui.m_title_buffer.push(&ui.m_arena, DEFAULT_TITLE, sizeof(DEFAULT_TITLE));

      ScratchArena scratch;
      Path default_location;
      const char *project_home = std::getenv("REN_PROJECT_HOME");
      if (project_home) {
        default_location = Path::init(scratch, String8::init(project_home));
      }
      if (!default_location) {
        default_location = app_data_directory(scratch).concat(
            scratch, {Path::init("ren"), Path::init("projects")});
      }
      if (IoResult<void> result = create_directories(default_location);
          !result) {
        fmt::println(stderr, "Failed to create {}: {}", default_location,
                     result.error());
      }

      ui.m_location_buffer = {};
      ui.m_location_buffer.push(&ui.m_arena, default_location.m_str.m_str,
                                default_location.m_str.m_size);
      ui.m_location_buffer.push(&ui.m_arena, 0);
    }

    ImGui::Text("Title:");
    InputText("##Title", &ui.m_arena, &ui.m_title_buffer);

    if (ui.m_dialog_active) {
      bool done =
          std::atomic_ref(ui.m_dialog_done).load(std::memory_order_acquire);
      if (done) {
        if (ui.m_dialog_path) {
          ui.m_location_buffer.clear();
          auto [str, size] = ui.m_dialog_path.m_str;
          ui.m_location_buffer.push(&ui.m_arena, str, size);
          ui.m_location_buffer.push(&ui.m_arena, 0);
        }
        ui.m_dialog_active = false;
        ui.m_dialog_done = false;
        ui.m_dialog_path = {};
        ui.m_dialog_arena.clear();
      }
    }

    ImGui::Text("Location:");
    InputText("##Location", &ui.m_arena, &ui.m_location_buffer);
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.m_dialog_active);
    if (ImGui::Button("Browse...")) {
      if (!ui.m_dialog_arena) {
        ui.m_dialog_arena = Arena::init();
      }
      if (!ui.m_dialog_properties) {
        ui.m_dialog_properties = SDL_CreateProperties();
        SDL_SetPointerProperty(ui.m_dialog_properties,
                               SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                               ctx->m_window);
        SDL_SetBooleanProperty(ui.m_dialog_properties,
                               SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
        SDL_SetStringProperty(ui.m_dialog_properties,
                              SDL_PROP_FILE_DIALOG_TITLE_STRING,
                              "New Project Location");
      }
      SDL_SetStringProperty(ui.m_dialog_properties,
                            SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                            ui.m_location_buffer.m_data);
      ui.m_dialog_active = true;
      SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER,
                                       new_project_dialog_callback, &ui,
                                       ui.m_dialog_properties);
    }
    ImGui::EndDisabled();

    ScratchArena scratch;
    Path location =
        Path::init(scratch, String8::init(ui.m_location_buffer.m_data));
    Path title = Path::init(scratch, String8::init(ui.m_title_buffer.m_data));
    Path path = location.concat(scratch, title);
    ImGui::Text("Path:");
    ImGui::Text("%.*s", (int)path.m_str.m_size, path.m_str.m_str);

    bool close = false;
    ImGui::BeginDisabled(ui.m_dialog_active);
    if (ImGui::Button("Create")) {
      bool success = new_project(path, title);
      close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      close = true;
    }
    ImGui::EndDisabled();

    if (close) {
      ui.m_arena.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
