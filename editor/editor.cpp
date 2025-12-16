#include "FileWatcher.hpp"
#include "Meta.hpp"
#include "UiWidgets.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Arena.hpp"
#include "ren/core/Assert.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/GenArray.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/Job.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"
#include "ren/core/glTF.hpp"
#include "ren/ren.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <imgui.hpp>
#include <tracy/Tracy.hpp>

namespace ren {

namespace {

const Path ASSET_DIR = Path::init("assets");
const Path GLTF_DIR = Path::init("glTF");
const Path META_EXT = Path::init(".meta");

const Path CONTENT_DIR = Path::init("content");
const Path MESH_DIR = Path::init("mesh");

} // namespace

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

enum class EditorState {
  Startup,
  Project,
  Quit,
};

struct NewProjectUI {
  DynamicArray<char> m_title_buffer;
  DynamicArray<char> m_location_buffer;
  String8 m_error;
};

struct OpenProjectUI {
  String8 m_error;
};

enum class ImportSceneUIState {
  Initial,
  Importing,
  Failed,
  Success,
};

struct ImportSceneUI {
  ImportSceneUIState m_state = ImportSceneUIState::Initial;
  DynamicArray<char> m_path_buffer;
  ArenaTag m_import_tag;
  JobFuture<Result<void, String8>> m_import_future;
  String8 m_import_error;
};

enum class MessageSeverity {
  Info,
  Error,
};

struct Message {
  MessageSeverity severity;
  StringDeck info;
};

struct EditorUI {
  ImFont *m_font = nullptr;
  NewProjectUI m_new_project;
  OpenProjectUI m_open_project;
  ImportSceneUI m_import_scene;
};

struct EditorBackgroundJob {
  JobToken token;
  ArenaTag tag;
};

template <typename T> struct HandleList {
  Handle<T> prev;
  Handle<T> next;

public:
  explicit operator bool() const {
    ren_assert((!prev and !next) or (prev and next));
    return prev;
  }
};

#define ren_handle_list_insert_after(gen_array, field, prev_handle,            \
                                     node_handle)                              \
  {                                                                            \
    auto &prev = gen_array[prev_handle];                                       \
    auto next_handle = prev.field.next;                                        \
    auto &node = gen_array[node_handle];                                       \
    auto &next = gen_array[next_handle];                                       \
    prev.field.next = node_handle;                                             \
    node.field.prev = prev_handle;                                             \
    node.field.next = next_handle;                                             \
    next.field.prev = node_handle;                                             \
  }

#define ren_handle_list_remove(gen_array, field, node_handle)                  \
  {                                                                            \
    auto &node = gen_array[node_handle];                                       \
    auto &prev = gen_array[node.field.prev];                                   \
    auto &next = gen_array[node.field.next];                                   \
    prev.field.next = node.field.next;                                         \
    next.field.prev = node.field.prev;                                         \
  }

#define ren_handle_list_init(gen_array, field, head_handle)                    \
  {                                                                            \
    auto &head = gen_array[head_handle];                                       \
    head.field = {.prev = head_handle, .next = head_handle};                   \
  }

struct EditorMesh;

struct EditorGltfScene {
  Path bin_filename;
  Path gltf_filename;
  Path meta_filename;
  Handle<EditorMesh> first_mesh;
  HandleList<EditorGltfScene> list;
};

struct EditorMesh {
  Guid64 guid = {};
  String8 name;
  Handle<EditorMesh> next;

  HandleList<EditorMesh> dirty_list;
};

struct EditorProjectContext {
  Path m_directory;
  DynamicArray<EditorBackgroundJob> m_background_jobs;

  GenArray<EditorGltfScene> m_gltf_scenes;
  GenArray<EditorMesh> m_meshes;

  Handle<EditorGltfScene> m_gltf_scenes_head;
  Handle<EditorMesh> m_mesh_dirty_list_head;

  FileWatcher *m_asset_watcher = nullptr;
};

struct EditorContext {
  Arena m_arena;
  Arena m_project_arena;
  Arena m_frame_arena;
  Arena m_popup_arena;
  Arena m_dialog_arena;

  Renderer *m_renderer = nullptr;
  SDL_Window *m_window = nullptr;
  SwapChain *m_swap_chain = nullptr;
  Scene *m_scene = nullptr;
  ren::Handle<Camera> m_camera;
  EditorState m_state = EditorState::Startup;
  EditorUI m_ui;
  EditorProjectContext *m_project = nullptr;

  DynamicArray<Path> m_recently_opened;
};

void load_recently_opened_list(NotNull<EditorContext *> ctx) {
  ScratchArena scratch;
  Path load_path = editor_recently_opened_list_path(scratch);
  IoResult<Span<char>> buffer = read(scratch, load_path);
  if (!buffer) {
    fmt::println(stderr, "Failed to read {}: {}", load_path, buffer.error());
    return;
  }
  String8 str(buffer->m_data, buffer->m_size);
  Span<String8> paths = str.split(scratch, '\n');
  ctx->m_recently_opened.clear();
  for (String8 path : paths) {
    path = path.strip_right('\r');
    if (!path) {
      continue;
    }
    ctx->m_recently_opened.push(&ctx->m_arena, Path::init(&ctx->m_arena, path));
  }
}

void save_recently_opened_list(NotNull<EditorContext *> ctx) {
  ScratchArena scratch;
  StringBuilder builder(scratch);
  Span<const Path> recent = ctx->m_recently_opened;
  if (recent.m_size == 0) {
    return;
  }
  for (Path path : recent) {
    builder.push(path.m_str);
    builder.push('\n');
  }
  Path save_path = editor_recently_opened_list_path(scratch);
  std::ignore = create_directories(save_path.parent());
  IoResult<void> result = write(save_path, builder.string());
  if (!result) {
    fmt::println(stderr, "Failed to write {}: {}", save_path, result.error());
  }
}

void init_editor(int argc, const char *argv[], NotNull<EditorContext *> ctx) {
  ScratchArena::init_for_thread();

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

  launch_job_server();

  load_recently_opened_list(ctx);
};

void register_gltf_scene(NotNull<EditorContext *> ctx, const MetaGltf &meta,
                         Path meta_filename) {
  ScratchArena scratch;
  EditorProjectContext *project = ctx->m_project;

  Path assets = project->m_directory.concat(scratch, {ASSET_DIR, GLTF_DIR});
  Path gltf_filename = meta_filename.remove_extension();
  Path bin_filename =
      gltf_filename.replace_extension(scratch, Path::init(".bin"));
  Path gtlf_path = assets.concat(scratch, gltf_filename);
  Path bin_path = assets.concat(scratch, bin_filename);
  Path meta_path = assets.concat(scratch, meta_filename);

  // Avoid endless recompilation loops if we can't read a file's modification
  // time by treating it as old as the universe itself.
  u64 gltf_mtime = last_write_time(gtlf_path).value_or(0);
  u64 bin_mtime = last_write_time(bin_path).value_or(0);
  u64 meta_mtime = last_write_time(meta_path).value_or(0);

  Path content = project->m_directory.concat(scratch, {CONTENT_DIR, MESH_DIR});

  Handle<EditorMesh> first_mesh_handle;
  for (MetaMesh meta_mesh : meta.meshes) {
    String8 guid_str = to_string(scratch, meta_mesh.guid);
    Path mesh_path = content.concat(scratch, Path::init(guid_str));
    u64 mtime = last_write_time(mesh_path).value_or(0);

    first_mesh_handle = project->m_meshes.insert(
        &ctx->m_project_arena,
        {
            .guid = meta_mesh.guid,
            .name = meta_mesh.name.copy(&ctx->m_project_arena),
            .next = first_mesh_handle,
        });
    if (mtime < max({gltf_mtime, bin_mtime, meta_mtime})) {
      ren_handle_list_insert_after(project->m_meshes, dirty_list,
                                   project->m_mesh_dirty_list_head,
                                   first_mesh_handle);
    }
  }
  Handle<EditorGltfScene> handle = project->m_gltf_scenes.insert(
      &ctx->m_project_arena,
      {
          /// FIXME: file names are leaked when a file is unregistered.
          .bin_filename = bin_filename.copy(&ctx->m_project_arena),
          .gltf_filename = gltf_filename.copy(&ctx->m_project_arena),
          .meta_filename = meta_filename.copy(&ctx->m_project_arena),
          .first_mesh = first_mesh_handle,
      });
  ren_handle_list_insert_after(project->m_gltf_scenes, list,
                               project->m_gltf_scenes_head, handle);
}

void register_gltf_scene(NotNull<EditorContext *> ctx, Path meta_path) {
  ren_assert(meta_path.is_absolute());
  ScratchArena scratch;
  IoResult<Span<char>> buffer = read(scratch, meta_path);
  if (!buffer) {
    fmt::println(stderr, "Failed to read {}: {}", meta_path, buffer.error());
    return;
  }
  Result<JsonValue, JsonErrorInfo> json =
      json_parse(scratch, {buffer->m_data, buffer->m_size});
  if (!json) {
    JsonErrorInfo error = json.error();
    fmt::println(stderr, "{}:{}:{}: {}", meta_path, error.line, error.column,
                 error.error);
    return;
  }
  Result<MetaGltf, MetaGltfErrorInfo> meta =
      meta_gltf_from_json(scratch, *json);
  if (!meta) {
    fmt::println(stderr, "Failed to parse meta file {}: ", meta_path,
                 to_string(scratch, meta.error()));
    return;
  }
  register_gltf_scene(ctx, *meta, meta_path.filename());
}

void unregister_gltf_scene(NotNull<EditorContext *> ctx, Path meta_filename) {
  // TODO: fix linear search, it's slow.
  EditorProjectContext *project = ctx->m_project;
  for (auto &&[handle, gltf_scene] : project->m_gltf_scenes) {
    if (gltf_scene.meta_filename == meta_filename) {
      Handle<EditorMesh> mesh_handle = gltf_scene.first_mesh;
      while (mesh_handle) {
        const EditorMesh &mesh = project->m_meshes[mesh_handle];
        Handle<EditorMesh> next = mesh.next;
        if (mesh.dirty_list) {
          ren_handle_list_remove(project->m_meshes, dirty_list, mesh_handle);
        }
        project->m_meshes.erase(mesh_handle);
        mesh_handle = next;
      }
      ren_handle_list_remove(project->m_gltf_scenes, list, handle);
      project->m_gltf_scenes.erase(handle);
      return;
    }
  }
}

void register_all_gltf_scenes(NotNull<EditorContext *> ctx) {
  ScratchArena scratch;
  Path assets =
      ctx->m_project->m_directory.concat(scratch, {ASSET_DIR, GLTF_DIR});
  IoResult<NotNull<Directory *>> dirit = open_directory(scratch, assets);
  if (!dirit) {
    fmt::println(stderr, "Failed to open {}: {}", assets, dirit.error());
    return;
  }
  while (true) {
    ScratchArena scratch;
    IoResult<Path> entry_result = read_directory(scratch, *dirit);
    if (!entry_result) {
      fmt::println(stderr, "Failed to read directory entry in {}: {}", assets,
                   entry_result.error());
      close_directory(*dirit);
      break;
    }
    Path entry = *entry_result;
    if (!entry) {
      break;
    }
    if (entry.extension() != META_EXT) {
      continue;
    }
    Path meta_path = assets.concat(scratch, entry);
    register_gltf_scene(ctx, meta_path);
  }
  close_directory(*dirit);
}

void unregister_all_gltf_scenes(NotNull<EditorContext *> ctx) {
  EditorProjectContext *project = ctx->m_project;

  project->m_gltf_scenes.clear();
  project->m_meshes.clear();

  project->m_gltf_scenes_head =
      project->m_gltf_scenes.insert(&ctx->m_project_arena);
  ren_handle_list_init(project->m_gltf_scenes, list,
                       project->m_gltf_scenes_head);
  project->m_mesh_dirty_list_head =
      project->m_meshes.insert(&ctx->m_project_arena);
  ren_handle_list_init(project->m_meshes, dirty_list,
                       project->m_mesh_dirty_list_head);
}

void mark_gltf_scene_dirty(NotNull<EditorContext *> ctx, Path meta_filename) {
  // TODO: fix linear search, it's slow.
  EditorProjectContext *project = ctx->m_project;
  for (auto &&[_, gltf_scene] : project->m_gltf_scenes) {
    if (gltf_scene.meta_filename == meta_filename) {
      Handle<EditorMesh> mesh_handle = gltf_scene.first_mesh;
      while (mesh_handle) {
        const EditorMesh &mesh = project->m_meshes[mesh_handle];
        Handle<EditorMesh> next = mesh.next;
        if (!mesh.dirty_list) {
          ren_handle_list_insert_after(project->m_meshes, dirty_list,
                                       project->m_mesh_dirty_list_head,
                                       mesh_handle);
        }
        mesh_handle = next;
      }
      return;
    }
  }
}

void mark_gltf_scene_not_dirty(NotNull<EditorContext *> ctx,
                               Path meta_filename) {
  // TODO: fix linear search, it's slow.
  EditorProjectContext *project = ctx->m_project;
  for (auto &&[_, gltf_scene] : project->m_gltf_scenes) {
    if (gltf_scene.meta_filename == meta_filename) {
      Handle<EditorMesh> mesh_handle = gltf_scene.first_mesh;
      while (mesh_handle) {
        const EditorMesh &mesh = project->m_meshes[mesh_handle];
        Handle<EditorMesh> next = mesh.next;
        if (mesh.dirty_list) {
          ren_handle_list_remove(project->m_meshes, dirty_list,

                                 mesh_handle);
        }
        mesh_handle = next;
      }
      return;
    }
  }
}

void start_asset_watcher(NotNull<EditorContext *> ctx) {
  ZoneScoped;
  EditorProjectContext *project = ctx->m_project;
  Path root = project->m_directory;
  project->m_asset_watcher = start_file_watcher(&ctx->m_project_arena, root);
  if (!project->m_asset_watcher) {
    return;
  }
  ScratchArena scratch;
  Path assets_relative_path = ASSET_DIR;
  Path gltf_relative_path = assets_relative_path.concat(scratch, GLTF_DIR);
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  assets_relative_path);
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  gltf_relative_path);
}

void stop_asset_watcher(NotNull<EditorContext *> ctx) {
  EditorProjectContext *project = ctx->m_project;
  if (!project->m_asset_watcher) {
    return;
  }
  stop_file_watcher(project->m_asset_watcher);
  project->m_asset_watcher = nullptr;
}

void run_asset_watcher(NotNull<EditorContext *> ctx) {
  ZoneScoped;

  EditorProjectContext *project = ctx->m_project;
  if (!project->m_asset_watcher) {
    return;
  }

  ScratchArena scratch;
  Path gltf_relative_path = ASSET_DIR.concat(scratch, GLTF_DIR);
  Path gltf_path =
      ctx->m_project->m_directory.concat(scratch, gltf_relative_path);
  while (true) {
    Optional<FileWatchEvent> event =
        read_watch_event(scratch, project->m_asset_watcher);
    if (!event) {
      return;
    }

    if (event->type == FileWatchEventType::QueueOverflow or
        (event->type == FileWatchEventType::Removed and !event->filename)) {
      stop_asset_watcher(ctx);
      unregister_all_gltf_scenes(ctx);
      start_asset_watcher(ctx);
      register_all_gltf_scenes(ctx);
      return;
    }

    if (event->parent == gltf_relative_path and event->filename) {
      if (event->filename.extension() == META_EXT) {
        if (event->type == FileWatchEventType::Removed) {
          unregister_gltf_scene(ctx, event->filename);
        } else if (event->type == FileWatchEventType::CreatedOrModified) {
          unregister_gltf_scene(ctx, event->filename);
          register_gltf_scene(ctx, gltf_path.concat(scratch, event->filename));
        }
      } else if (event->filename.extension() == ".gltf" or
                 event->filename.extension() == ".bin") {
        Path gltf_filename =
            event->filename.replace_extension(scratch, ".gltf");
        Path meta_filename = gltf_filename.add_extension(scratch, META_EXT);
        if (event->type == FileWatchEventType::Removed) {
          mark_gltf_scene_not_dirty(ctx, meta_filename);
        } else if (event->type == FileWatchEventType::CreatedOrModified) {
          mark_gltf_scene_dirty(ctx, meta_filename);
        }
      }
    }
  }
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

  Handle<EditorGltfScene> gltf_scene_list_head =
      ctx->m_project->m_gltf_scenes.insert(&ctx->m_project_arena);
  ctx->m_project->m_gltf_scenes_head = gltf_scene_list_head;
  ctx->m_project->m_gltf_scenes[gltf_scene_list_head].list = {
      .prev = gltf_scene_list_head,
      .next = gltf_scene_list_head,
  };

  Handle<EditorMesh> mesh_dirty_list_head =
      ctx->m_project->m_meshes.insert(&ctx->m_project_arena);
  ctx->m_project->m_mesh_dirty_list_head = mesh_dirty_list_head;
  ctx->m_project->m_meshes[mesh_dirty_list_head].dirty_list = {
      .prev = mesh_dirty_list_head,
      .next = mesh_dirty_list_head,
  };
  ctx->m_state = EditorState::Project;
  start_asset_watcher(ctx);
  register_all_gltf_scenes(ctx);

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
    job_free_tag(&tag);
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

template <typename T>
Span<const T> accessor_data(Span<const std::byte> bin, JsonValue gltf,
                            usize accessor_index) {
  Span<const JsonValue> accessors = json_array_value(gltf, "accessors");
  Span<const JsonValue> buffer_views = json_array_value(gltf, "bufferViews");
  JsonValue accessor = accessors[accessor_index];
  JsonValue buffer_view =
      buffer_views[json_integer_value(accessor, "bufferView")];
  ren_assert(json_integer_value(buffer_view, "buffer") == 0);
  usize in_view_offset = json_integer_value(accessor, "byteOffset");
  usize bin_offset =
      json_integer_value(buffer_view, "byteOffset") + in_view_offset;
  usize count = json_integer_value(accessor, "count");
  usize component_size = 0;
  switch ((GltfComponentType)json_integer_value(accessor, "componentType")) {
  case GLTF_COMPONENT_BYTE:
    component_size = sizeof(i8);
    break;
  case GLTF_COMPONENT_UNSIGNED_BYTE:
    component_size = sizeof(u8);
    break;
  case GLTF_COMPONENT_SHORT:
    component_size = sizeof(i16);
    break;
  case GLTF_COMPONENT_UNSIGNED_SHORT:
    component_size = sizeof(u16);
    break;
  case GLTF_COMPONENT_UNSIGNED_INT:
    component_size = sizeof(u32);
    break;
  case GLTF_COMPONENT_FLOAT:
    component_size = sizeof(float);
    break;
  }
  String8 accessor_type = json_string_value(accessor, "type");
  usize component_count = 0;
  if (accessor_type == GLTF_ACCESSOR_TYPE_SCALAR) {
    component_count = 1;
  } else if (accessor_type == GLTF_ACCESSOR_TYPE_VEC2) {
    component_count = 2;
  } else if (accessor_type == GLTF_ACCESSOR_TYPE_VEC3) {
    component_count = 3;
  } else if (accessor_type == GLTF_ACCESSOR_TYPE_VEC4) {
    component_count = 4;
  } else if (accessor_type == GLTF_ACCESSOR_TYPE_MAT2) {
    component_count = 2 * 2;
  } else if (accessor_type == GLTF_ACCESSOR_TYPE_MAT3) {
    component_count = 3 * 3;
  } else {
    ren_assert(accessor_type == GLTF_ACCESSOR_TYPE_MAT4);
    component_count = 4 * 4;
  }
  ren_assert(sizeof(T) == component_size * component_count);
  return {(const T *)&bin[bin_offset], count};
}

Span<std::byte> process_mesh(NotNull<Arena *> arena, JsonValue gltf,
                             Span<const std::byte> bin, usize mesh_index,
                             usize primitive_index) {
  JsonValue gltf_mesh = json_array_value(gltf, "meshes")[mesh_index];
  JsonValue gltf_primitive =
      json_array_value(gltf_mesh, "primitives")[primitive_index];
  JsonValue attributes = json_value(gltf_primitive, "attributes");
  auto positions = accessor_data<glm::vec3>(
      bin, gltf, json_integer_value(attributes, "POSITION"));
  auto normals = accessor_data<glm::vec3>(
      bin, gltf, json_integer_value(attributes, "NORMAL"));
  Span<const glm::vec4> tangents;
  JsonValue tangent_accessor = json_value(attributes, "TANGENT");
  if (tangent_accessor) {
    tangents =
        accessor_data<glm::vec4>(bin, gltf, json_integer(tangent_accessor));
  }
  Span<const glm::vec2> uvs;
  JsonValue uv_accessor = json_value(attributes, "TEXCOORD_0");
  if (uv_accessor) {
    uvs = accessor_data<glm::vec2>(bin, gltf, json_integer(uv_accessor));
  }
  Span<const glm::vec4> colors;
  JsonValue color_accessor = json_value(attributes, "COLOR_0");
  if (color_accessor) {
    colors = accessor_data<glm::vec4>(bin, gltf, json_integer(color_accessor));
  }
  auto indices = accessor_data<const u32>(
      bin, gltf, json_integer_value(gltf_primitive, "indices"));
  Blob blob = bake_mesh_to_memory(arena, {
                                             .num_vertices = positions.m_size,
                                             .positions = positions.m_data,
                                             .normals = normals.m_data,
                                             .tangents = tangents.m_data,
                                             .uvs = uvs.m_data,
                                             .colors = colors.m_data,
                                             .indices = indices,
                                         });
  return {(std::byte *)blob.data, blob.size};
}

[[nodiscard]] JobFuture<Result<void, String8>>
job_import_scene(NotNull<EditorContext *> ctx, ArenaTag tag, Path path) {
  JobFuture<Result<void, String8>> future = job_dispatch(
      "Import Scene", tag, [ctx, tag, path]() -> Result<void, String8> {
        ScratchArena scratch;
        Arena output = Arena::from_tag(tag);

        Path scene_directory =
            ctx->m_project->m_directory.concat(scratch, {ASSET_DIR, GLTF_DIR});
        Path filename = path.filename();
        Path gltf_filename =
            filename.replace_extension(scratch, Path::init(".gltf"));
        Path bin_filename =
            filename.replace_extension(scratch, Path::init(".bin"));
        Path meta_filename = gltf_filename.add_extension(scratch, META_EXT);
        Path gltf_path = scene_directory.concat(scratch, gltf_filename);
        Path bin_path = scene_directory.concat(scratch, bin_filename);
        Path meta_path = scene_directory.concat(scratch, meta_filename);
        Path blob_directory = ctx->m_project->m_directory.concat(
            scratch, {CONTENT_DIR, MESH_DIR});

        if (IoResult<void> result = create_directories(scene_directory);
            !result) {
          return format(&output, "Failed to create {}: {}", scene_directory,
                        result.error());
        }
        if (IoResult<void> result = create_directories(blob_directory);
            !result) {
          return format(&output, "Failed to create {}: {}", blob_directory,
                        result.error());
        }

        // clang-format off
    Assimp::Importer importer;
    importer.SetPropertyInteger(
        AI_CONFIG_PP_RVC_FLAGS,
        aiComponent_MATERIALS |
        aiComponent_CAMERAS |
        aiComponent_TEXTURES |
        aiComponent_LIGHTS
    );
    const aiScene *scene = importer.ReadFile(
        path.m_str.zero_terminated(scratch),
        aiProcess_FindInstances |
        aiProcess_FindInvalidData |
        aiProcess_GenNormals |
        aiProcess_OptimizeGraph |
        aiProcess_RemoveComponent |
        aiProcess_SortByPType |
        aiProcess_Triangulate
    );
        // clang-format on
        if (!scene) {
          return String8::init(&output, importer.GetErrorString());
        }
        ren_assert(scene->mNumMeshes > 0);

        Assimp::Exporter exporter;
        Assimp::ExportProperties exporter_properties;
        exporter_properties.SetPropertyString(
            AI_CONFIG_EXPORT_BLOB_NAME,
            filename.stem().m_str.zero_terminated(scratch));
        const aiExportDataBlob *blob =
            exporter.ExportToBlob(scene, "gltf2", 0, &exporter_properties);
        if (!blob) {
          return String8::init(&output, exporter.GetErrorString());
        }
        const aiExportDataBlob *bin = blob->next;

        if (auto result = write(gltf_path, blob->data, blob->size); !result) {
          return format(&output, "Failed to write {}: {}", gltf_path,
                        result.error());
        }
        if (auto result = write(bin_path, bin->data, bin->size); !result) {
          return format(&output, "Failed to write {}: {}", bin_path,
                        result.error());
        }

        Result<JsonValue, JsonErrorInfo> gltf =
            json_parse(scratch, String8((const char *)blob->data, blob->size));
        if (!gltf) {
          JsonErrorInfo error = gltf.error();
          return format(&output, "Failed to parse glTF:\n{}:{}:{}: {}",
                        gltf_path, error.line, error.column, error.error);
        }

        MetaGltf meta = meta_gltf_generate(scratch, *gltf, gltf_filename);
        if (auto result = write(
                meta_path, json_serialize(scratch, to_json(scratch, meta)));
            !result) {
          return format(&output, "Failed to write {}: {}", meta_path,
                        result.error());
        }

        return {};
      });
  ctx->m_project->m_background_jobs.push(&ctx->m_project_arena,
                                         {future.m_token, tag});
  return future;
}

void draw_editor_ui(NotNull<EditorContext *> ctx) {
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGui::PushFont(ctx->m_ui.m_font);

#if 0
  bool open = true;
  ImGui::ShowDemoWindow(&open);
#endif

  const char *NEW_PROJECT_POPUP_TEXT = "New Project";
  ImGuiID new_project_popup = ImGui::GetID(NEW_PROJECT_POPUP_TEXT);
  const char *OPEN_PROJECT_FAILED_POPUP_TEXT = "Open Project Failed";
  ImGuiID open_project_failed_popup =
      ImGui::GetID(OPEN_PROJECT_FAILED_POPUP_TEXT);
  const char *IMPORT_SCENE_POPUP_TEXT = "Import Scene";
  ImGuiID import_scene_popup = ImGui::GetID(IMPORT_SCENE_POPUP_TEXT);

  if (ImGui::BeginMainMenuBar()) {
    FileDialogGuid open_project_file_dialog_guid =
        FileDialogGuidFromName("Open Project");
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        ImGui::OpenPopup(new_project_popup);
      }

      if (ImGui::MenuItem("Open...")) {
        OpenProjectUI &ui = ctx->m_ui.m_open_project;
        ui = {};

        ScratchArena scratch;
        OpenFileDialog({
            .guid = open_project_file_dialog_guid,
            .type = FileDialogType::OpenFile,
            .modal_window = ctx->m_window,
            .start_path = editor_default_project_directory(scratch),
            .filters = {{.name = "Ren Project Files", .pattern = "json"}},
        });
      }

      if (ImGui::BeginMenu("Recent Projects",
                           ctx->m_recently_opened.m_size > 0)) {
        ScratchArena scratch;
        constexpr i32 NUM_RECENT = 5;
        Span<const Path> recent = ctx->m_recently_opened;
        isize oldest_index = max<isize>(recent.m_size - NUM_RECENT, 0);
        for (isize i = (isize)recent.m_size - 1; i >= oldest_index; --i) {
          Path path = recent[i];
          if (ImGui::MenuItem(path.m_str.zero_terminated(scratch))) {
            close_project(ctx);
            Result<void, String8> open_result = open_project(ctx, path);
            if (!open_result) {
              ImGui::OpenPopup(open_project_failed_popup);
              ctx->m_ui.m_open_project.m_error = open_result.error();
            }
          }
        }
        ImGui::EndMenu();
      }

      ImGui::BeginDisabled(!ctx->m_project);
      if (ImGui::MenuItem("Close")) {
        close_project(ctx);
      }
      ImGui::EndDisabled();

      if (ImGui::MenuItem("Quit")) {
        ctx->m_state = EditorState::Quit;
      }
      ImGui::EndMenu();
    }

    if (IsFileDialogDone(open_project_file_dialog_guid)) {
      ScratchArena scratch;
      Path path =
          FileDialogCopyPathAndClose(scratch, open_project_file_dialog_guid);
      if (path) {
        close_project(ctx);
        Result<void, String8> open_result = open_project(ctx, path);
        if (!open_result) {
          ImGui::OpenPopup(open_project_failed_popup);
          ctx->m_ui.m_open_project.m_error = open_result.error();
        }
      }
    }

    if (ctx->m_state == EditorState::Project) {
      if (ImGui::BeginMenu("Import")) {
        if (ImGui::MenuItem("Scene...")) {
          ImGui::OpenPopup(import_scene_popup);
        }
        ImGui::EndMenu();
      }
    }
  }
  float menu_height = ImGui::GetWindowHeight();
  ImGui::EndMainMenuBar();

  if (ctx->m_project) {
    constexpr ImGuiWindowFlags SIDE_PANEL_FLAGS =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoScrollWithMouse;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 side_panel_pos = {0, viewport->Size.y};
    ImGui::SetNextWindowPos(side_panel_pos, ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImVec2 side_panel_size = {
        viewport->Size.x * 0.2f,
        viewport->Size.y - menu_height,
    };
    ImGui::SetNextWindowSize(side_panel_size);

    if (ImGui::Begin("##assets", nullptr, SIDE_PANEL_FLAGS)) {
      if (ImGui::BeginTabBar("Asset tab bar",
                             ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                                 ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("Scene", nullptr)) {
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Meshes", nullptr)) {
          if (ImGui::BeginChild("##tree", {0.0f, 0.0f}, ImGuiChildFlags_None,
                                ImGuiWindowFlags_None)) {
            ScratchArena scratch;
            Handle<EditorGltfScene> head = ctx->m_project->m_gltf_scenes_head;
            Handle<EditorGltfScene> handle =
                ctx->m_project->m_gltf_scenes[head].list.next;
            while (handle != head) {
              const EditorGltfScene &scene =
                  ctx->m_project->m_gltf_scenes[handle];
              bool is_expanded = ImGui::TreeNodeEx(
                  scene.gltf_filename.m_str.zero_terminated(scratch),
                  ImGuiTreeNodeFlags_DefaultOpen);
              if (ImGui::BeginPopupContextItem()) {
                if (ImGui::Button("Delete")) {
                  Path assets = ctx->m_project->m_directory.concat(
                      scratch, {ASSET_DIR, GLTF_DIR});
                  Path bin_path = assets.concat(scratch, scene.bin_filename);
                  Path gltf_path = assets.concat(scratch, scene.gltf_filename);
                  Path meta_path = assets.concat(scratch, scene.meta_filename);
                  for (Path path : {bin_path, gltf_path, meta_path}) {
                    if (IoResult<void> result = unlink(path); !result) {
                      fmt::println(stderr, "Failed to delete {}: {}", path,
                                   result.error());
                    }
                  }
                  ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
              }
              if (is_expanded) {
                Handle<EditorMesh> mesh_handle = scene.first_mesh;
                while (mesh_handle) {
                  const EditorMesh &mesh =
                      ctx->m_project->m_meshes[mesh_handle];
                  if (ImGui::TreeNodeEx(mesh.name.zero_terminated(scratch),
                                        ImGuiTreeNodeFlags_Leaf |
                                            ImGuiTreeNodeFlags_Bullet)) {
                    ImGui::TreePop();
                  }
                  mesh_handle = mesh.next;
                }
                ImGui::TreePop();
              }
              handle = scene.list.next;
            }
          }
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
    }
    ImGui::End();
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(OPEN_PROJECT_FAILED_POPUP_TEXT, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    String8 error = ctx->m_ui.m_open_project.m_error;
    ren_assert(error);
    ImGui::Text("Opening project failed:\n%.*s", (int)error.m_size,
                error.m_str);
    if (ImGui::Button("OK")) {
      ImGui::CloseCurrentPopup();
      ctx->m_popup_arena.clear();
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(NEW_PROJECT_POPUP_TEXT, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ScratchArena scratch;

    NewProjectUI &ui = ctx->m_ui.m_new_project;
    if (ImGui::IsWindowAppearing()) {
      ui = {};
      const char DEFAULT_TITLE[] = "New Project";
      ui.m_title_buffer.push(&ctx->m_popup_arena, DEFAULT_TITLE,
                             sizeof(DEFAULT_TITLE));
      Path default_dir = editor_default_project_directory(scratch);
      if (not default_dir.exists().value_or(false)) {
        std::ignore = create_directories(default_dir);
      }
    }

    ImGui::Text("Title:");
    InputText("##Title", &ctx->m_popup_arena, &ui.m_title_buffer);

    FileDialogGuid file_dialog_guid = FileDialogGuidFromName("New Project");
    InputPath("Location", &ctx->m_popup_arena, &ui.m_location_buffer,
              {
                  .guid = file_dialog_guid,
                  .type = FileDialogType::OpenFolder,
                  .modal_window = ctx->m_window,
                  .start_path = editor_default_project_directory(scratch),
              });

    Path location =
        Path::init(scratch, String8::init(ui.m_location_buffer.m_data));
    Path title = Path::init(scratch, String8::init(ui.m_title_buffer.m_data));
    Path path = location.concat(scratch, title);
    ImGui::Text("Path:");
    ImGui::Text("%.*s", (int)path.m_str.m_size, path.m_str.m_str);

    if (ui.m_error) {
      ImGui::Text("Project creation failed:\n%.*s", (int)ui.m_error.m_size,
                  ui.m_error.m_str);
    }

    bool close = false;
    ImGui::BeginDisabled(IsFileDialogOpen(file_dialog_guid));
    if (ImGui::Button("Create")) {
      close_project(ctx);
      Result<void, String8> result = new_project(ctx, path);
      if (result) {
        close = true;
      } else {
        ui.m_error = result.error();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      close = true;
    }
    ImGui::EndDisabled();

    if (close) {
      ImGui::CloseCurrentPopup();
      ctx->m_popup_arena.clear();
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(IMPORT_SCENE_POPUP_TEXT, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ScratchArena scratch;
    ImportSceneUI &ui = ctx->m_ui.m_import_scene;
    if (ImGui::IsWindowAppearing()) {
      ui = {};
    }

    ImGui::BeginDisabled(ui.m_state == ImportSceneUIState::Importing);
    FileDialogGuid file_dialog_guid = FileDialogGuidFromName("Import Scene");
    InputPath("Path", &ctx->m_popup_arena, &ui.m_path_buffer,
              {
                  .guid = file_dialog_guid,
                  .type = FileDialogType::OpenFile,
                  .modal_window = ctx->m_window,
              });
    ImGui::EndDisabled();

    if (ui.m_import_future and ui.m_import_future.is_ready()) {
      Result<void, String8> &import_result = *ui.m_import_future;
      ui.m_state = import_result ? ImportSceneUIState::Success
                                 : ImportSceneUIState::Failed;
      if (!import_result) {
        ui.m_import_error = import_result.error().copy(&ctx->m_popup_arena);
      }
      ui.m_import_future = {};
      job_free_tag(&ui.m_import_tag);
    }

    if (ui.m_state == ImportSceneUIState::Importing) {
      ImGui::Text("Importing...");
    } else if (ui.m_state == ImportSceneUIState::Failed) {
      ImGui::Text("Import failed:\n%.*s", (int)ui.m_import_error.m_size,
                  ui.m_import_error.m_str);
    } else if (ui.m_state == ImportSceneUIState::Success) {
      ImGui::Text("Import succeeded!");
    }

    bool close = false;
    if (ui.m_state == ImportSceneUIState::Success) {
      if (ImGui::Button("Close")) {
        close = true;
      }
    } else {
      ImGui::BeginDisabled(IsFileDialogOpen(file_dialog_guid) or
                           ui.m_state == ImportSceneUIState::Importing);
      if (ImGui::Button("Import")) {
        ui.m_state = ImportSceneUIState::Importing;
        ui.m_import_tag = job_new_tag();
        Path path = Path::init(&ctx->m_popup_arena,
                               String8::init(ui.m_path_buffer.m_data));
        ui.m_import_future = job_import_scene(ctx, ui.m_import_tag, path);
        ui.m_import_error = {};
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        close = true;
      }
      ImGui::EndDisabled();
    }

    if (close) {
      ImGui::CloseCurrentPopup();
      ctx->m_popup_arena.clear();
    }

    ImGui::EndPopup();
  }

  ImGui::PopFont();
  ImGui::Render();
  ImGui::EndFrame();
}

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
  stop_job_server();

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
