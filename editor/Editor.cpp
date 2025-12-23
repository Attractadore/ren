#include "Editor.hpp"
#include "AssetWatcher.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/Format.hpp"

#include <SDL3/SDL_init.h>
#include <fmt/base.h>
#include <imgui.hpp>

namespace ren {

Path editor_settings_directory(NotNull<Arena *> arena) {
  ScratchArena scratch;
  return app_data_directory(scratch).concat(
      arena, {Path::init("ren"), Path::init("editor")});
}

Path editor_recently_opened_list_path(NotNull<Arena *> arena) {
  ScratchArena scratch;
  return editor_settings_directory(scratch).concat(
      arena, Path::init("recently-opened.txt"));
}

Path editor_default_project_directory(NotNull<Arena *> arena) {
  const char *project_home = std::getenv("REN_PROJECT_HOME");
  if (project_home) {
    return Path::init(arena, String8::init(project_home));
  }
  ScratchArena scratch;
  return app_data_directory(scratch).concat(
      arena, {Path::init("ren"), Path::init("projects")});
}

void init_editor(int argc, const char *argv[], NotNull<EditorContext *> ctx) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fmt::println(stderr, "Failed to init SDL3: {}", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  ctx->m_arena = Arena::init();
  ctx->m_project_arena = Arena::init();
  ctx->m_frame_arena = Arena::init();
  ctx->m_popup_arena = Arena::init();
  ctx->m_dialog_arena = Arena::init();

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
  ctx->m_ui.m_font = io.Fonts->AddFont(&font_config);
  io.Fonts->Build();
  io.FontGlobalScale = 1.0f / pixel_density;

  ImGui::GetStyle().ScaleAllSizes(display_scale / pixel_density);

  if (!ImGui_ImplSDL3_InitForVulkan(ctx->m_window)) {
    fmt::println(stderr, "Failed to init ImGui backend");
    exit(EXIT_FAILURE);
  }

  ren::init_imgui(&ctx->m_frame_arena, ctx->m_scene);

  load_recently_opened_list(ctx);
};

void run_editor(NotNull<EditorContext *> ctx) {
  u64 time = clock();
  while (ctx->m_state != EditorState::Quit) {
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
        ctx->m_state = EditorState::Quit;
        break;
      }
    }

    {
      ScratchArena scratch;
      String8 title = "ren editor";
      if (ctx->m_project) {
        title = format(scratch, "ren editor: {}", ctx->m_project->m_directory);
      }
      SDL_SetWindowTitle(ctx->m_window, title.zero_terminated(scratch));
    }

    if (ctx->m_project) {
      for (usize i = 0; i < ctx->m_project->m_background_jobs.m_size;) {
        if (job_is_done(ctx->m_project->m_background_jobs[i].token)) {
          ctx->m_project->m_background_jobs[i] =
              ctx->m_project->m_background_jobs.back();
          ctx->m_project->m_background_jobs.pop();
        } else {
          i++;
        }
      }
      run_asset_watcher(ctx);
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

Result<void, String8> open_project(NotNull<EditorContext *> ctx, Path path) {
  ScratchArena scratch;

  IoResult<Path> abs_path = path.absolute(&ctx->m_arena);
  if (abs_path) {
    DynamicArray<Path> &recent = ctx->m_recently_opened;
    for (usize i : range(recent.m_size)) {
      if (recent[i] == *abs_path) {
        copy_overlapped(recent.m_data + i + 1, recent.m_size - i - 1,
                        &recent[i]);
        recent.pop();
        break;
      }
    }
  }

  if (not path.exists().value_or(false)) {
    return format(&ctx->m_popup_arena, "Failed to open {}", path);
  }
  ctx->m_project = ctx->m_project_arena.allocate<EditorProjectContext>();
  *ctx->m_project = {
      .m_directory = path.parent().copy(&ctx->m_project_arena),
      .m_gltf_scenes = GenArray<EditorGltfScene>::init(&ctx->m_project_arena),
      .m_meshes = GenArray<EditorMesh>::init(&ctx->m_project_arena),
  };
  ctx->m_state = EditorState::Project;
  start_asset_watcher(ctx);
  register_all_assets(ctx);

  if (abs_path) {
    ctx->m_recently_opened.push(&ctx->m_arena, *abs_path);
    save_recently_opened_list(ctx);
  }

  return {};
}

void close_project(NotNull<EditorContext *> ctx) {
  if (!ctx->m_project) {
    return;
  }
  for (auto [token, tag] : ctx->m_project->m_background_jobs) {
    job_wait(token);
    job_reset_tag(tag);
  }
  stop_asset_watcher(ctx);
  ctx->m_state = EditorState::Startup;
  ctx->m_project = nullptr;
  ctx->m_project_arena.clear();
  job_reset_tag(ArenaNamedTag::EditorProject);
}

Result<void, String8> new_project(NotNull<EditorContext *> ctx,
                                  Path project_directory) {
  ScratchArena scratch;
  Path title = project_directory.filename();
  Path project_path = project_directory.concat(
      scratch, title.replace_extension(scratch, Path::init(".json")));
  if (project_directory.exists().value_or(false)) {
    if (not is_directory_empty(project_directory).value_or(true)) {
      return format(&ctx->m_popup_arena, "{} is not empty!", project_directory);
    }
  }
  if (IoResult<void> result = create_directories(project_directory); !result) {
    return format(&ctx->m_popup_arena, "Failed to create directory {}: {}",
                  project_directory, result.error());
  }
  const char buffer[] = {'{', '\n', '}', '\n'};
  if (IoResult<void> result = write(project_path, Span(buffer)); !result) {
    return format(&ctx->m_popup_arena, "Failed to create {}: {}", project_path,
                  result.error());
  }
  return open_project(ctx, project_path);
}

} // namespace ren
