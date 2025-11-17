#include "ren/baking/mesh.hpp"
#include "ren/core/Algorithm.hpp"
#include "ren/core/Assert.hpp"
#include "ren/core/Chrono.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/JSON.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/glTF.hpp"
#include "ren/ren.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <atomic>
#include <blake3.h>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <imgui.hpp>

namespace ren {

namespace {

const Path ASSET_DIR = Path::init("assets");
const Path SCENE_DIR = Path::init("scene");

const Path CONTENT_DIR = Path::init("content");
const Path MESH_DIR = Path::init("mesh");

template <usize Bytes> struct alignas(u32) Guid {
  u8 m_data[Bytes] = {};
};

using Guid32 = Guid<4>;
using Guid64 = Guid<8>;
using Guid128 = Guid<16>;

template <usize Bytes>
String8 to_string(NotNull<Arena *> arena, Guid<Bytes> guid) {
  ScratchArena scratch;
  auto builder = StringBuilder::init(scratch);
  for (isize i = isize(Bytes) - 1; i >= 0; --i) {
    const char MAP[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    };
    u8 b = guid.m_data[i];
    u8 hi = b >> 4;
    u8 lo = b & 0xf;
    builder.push(MAP[hi]);
    builder.push(MAP[lo]);
  }
  return builder.materialize(arena);
}

} // namespace

enum class EditorState {
  Startup,
  Project,
  Quit,
};

enum class EditorPopupMenu {
  None,
  NewProject,
  ImportScene,
};

enum class EditorDialogClient {
  None,
  OpenProject,
};

struct NewProjectUI {
  DynamicArray<char> m_title_buffer;
  DynamicArray<char> m_location_buffer;
  String8 m_error;
};

struct OpenProjectUI {
  String8 m_error;
};

struct ImportSceneUI {
  DynamicArray<char> m_path_buffer;
  String8 m_import_error;
};

struct EditorUI {
  ImFont *m_font = nullptr;

  SDL_PropertiesID m_new_project_dialog_properties = 0;
  SDL_PropertiesID m_open_project_dialog_properties = 0;
  SDL_PropertiesID m_import_scene_dialog_properties = 0;

  EditorDialogClient m_dialog_client = EditorDialogClient::None;
  bool m_dialog_active = false;
  alignas(std::atomic<bool>) bool m_dialog_done = false;
  Path m_dialog_path;
  NewProjectUI m_new_project;
  OpenProjectUI m_open_project;
  ImportSceneUI m_import_scene;
};

struct EditorProjectContext {
  Path m_directory;
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
};

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
          while (buf->m_size < (u32)data->BufTextLen + 1) {
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

void SDLCALL open_file_dialog_callback(void *userdata,
                                       const char *const *filelist,
                                       int filter) {
  auto *ctx = (EditorContext *)userdata;
  if (!filelist) {
    fmt::println(stderr, "Failed to select file: {}", SDL_GetError());
  } else {
    const char *file = *filelist;
    if (file) {
      ctx->m_ui.m_dialog_path =
          Path::init(&ctx->m_dialog_arena, String8::init(file));
    }
  }
  std::atomic_ref(ctx->m_ui.m_dialog_done)
      .store(true, std::memory_order_release);
}

void SDLCALL open_folder_dialog_callback(void *userdata,
                                         const char *const *filelist,
                                         int filter) {
  auto *ctx = (EditorContext *)userdata;
  if (!filelist) {
    fmt::println(stderr, "Failed to select folder: {}", SDL_GetError());
  } else {
    const char *file = *filelist;
    if (file) {
      ctx->m_ui.m_dialog_path =
          Path::init(&ctx->m_dialog_arena, String8::init(file));
    }
  }
  std::atomic_ref(ctx->m_ui.m_dialog_done)
      .store(true, std::memory_order_release);
}

struct DialogFilter {
  String8 name;
  String8 pattern;
};

struct OpenDialogSettings {
  EditorDialogClient client = EditorDialogClient::None;
  SDL_FileDialogType type;
  SDL_PropertiesID properties;
  const char *location = nullptr;
  // TODO: implement filters.
  Span<const DialogFilter> filters;
};

void open_file_dialog(NotNull<EditorContext *> ctx,
                      const OpenDialogSettings &settings) {
  if (ctx->m_ui.m_dialog_active) {
    return;
  }
  SDL_DialogFileFilter *dialog_filters = nullptr;
  if (settings.filters.m_size > 0) {
    dialog_filters = ctx->m_dialog_arena.allocate<SDL_DialogFileFilter>(
        settings.filters.m_size);
    for (usize i : range(settings.filters.m_size)) {
      DialogFilter filter = settings.filters[i];
      dialog_filters[i] = {
          .name = filter.name.zero_terminated(&ctx->m_dialog_arena),
          .pattern = filter.pattern.zero_terminated(&ctx->m_dialog_arena),
      };
    }
    SDL_SetPointerProperty(settings.properties,
                           SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                           dialog_filters);
    SDL_SetNumberProperty(settings.properties,
                          SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          settings.filters.m_size);
  }
  ctx->m_ui.m_dialog_client = settings.client;
  ctx->m_ui.m_dialog_active = true;
  switch (settings.type) {
  case SDL_FILEDIALOG_OPENFILE: {
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE,
                                     open_file_dialog_callback, ctx,
                                     settings.properties);
  } break;
  case SDL_FILEDIALOG_SAVEFILE:
    ren_assert_msg(false, "Not implemented");
  case SDL_FILEDIALOG_OPENFOLDER:
    ren_assert(settings.location);
    SDL_SetStringProperty(settings.properties,
                          SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                          settings.location);
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER,
                                     open_folder_dialog_callback, ctx,
                                     settings.properties);
    break;
  }
  SDL_SetPointerProperty(settings.properties,
                         SDL_PROP_FILE_DIALOG_FILTERS_POINTER, nullptr);
  SDL_SetNumberProperty(settings.properties,
                        SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 0);
  SDL_SetStringProperty(settings.properties,
                        SDL_PROP_FILE_DIALOG_LOCATION_STRING, nullptr);
}

Path check_dialog_path(NotNull<Arena *> arena, NotNull<EditorContext *> ctx) {
  Path path;
  if (ctx->m_ui.m_dialog_active) {
    bool done = std::atomic_ref(ctx->m_ui.m_dialog_done)
                    .load(std::memory_order_acquire);
    if (done) {
      path = ctx->m_ui.m_dialog_path.copy(arena);
      ctx->m_ui.m_dialog_active = false;
      ctx->m_ui.m_dialog_done = false;
      ctx->m_ui.m_dialog_path = {};
      ctx->m_dialog_arena.clear();
    }
  }
  return path;
}

void InputPath(String8 name, NotNull<EditorContext *> ctx,
               NotNull<DynamicArray<char> *> buffer,
               SDL_FileDialogType dialog_type,
               SDL_PropertiesID dialog_properties) {
  ScratchArena scratch;
  if (Path dialog_path = check_dialog_path(scratch, ctx)) {
    buffer->clear();
    auto [str, size] = dialog_path.m_str;
    buffer->push(&ctx->m_popup_arena, str, size);
    buffer->push(&ctx->m_popup_arena, 0);
  }

  ImGui::Text("%.*s:", (int)name.m_size, name.m_str);
  InputText(format(scratch, "##{}", name).zero_terminated(scratch),
            &ctx->m_popup_arena, buffer);
  ImGui::SameLine();
  ImGui::BeginDisabled(ctx->m_ui.m_dialog_active);
  if (ImGui::Button("Browse...")) {
    open_file_dialog(ctx, {
                              .type = dialog_type,
                              .properties = dialog_properties,
                              .location = buffer->m_data,
                          });
  }
  ImGui::EndDisabled();
}

} // namespace

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
};

bool new_project(NotNull<EditorContext *> ctx, Path project_directory) {
  ScratchArena scratch;
  NewProjectUI &ui = ctx->m_ui.m_new_project;
  Path title = project_directory.filename();
  Path project_path = project_directory.concat(
      scratch, title.replace_extension(scratch, Path::init(".json")));
  if (project_directory.exists().value_or(false)) {
    if (not is_directory_empty(project_directory).value_or(true)) {
      ui.m_error =
          format(&ctx->m_popup_arena, "{} is not empty!", project_directory);
      return false;
    }
  }
  if (IoResult<void> result = create_directories(project_directory); !result) {
    ui.m_error =
        format(&ctx->m_popup_arena, "Failed to create directory {}: {}",
               project_directory, result.error());
    return false;
  }
  const char buffer[] = {'{', '\n', '}', '\n'};
  if (IoResult<void> result = write(project_path, Span(buffer)); !result) {
    ui.m_error = format(&ctx->m_popup_arena, "Failed to create {}: {}",
                        project_path, result.error());
    return false;
  }
  ctx->m_project = ctx->m_project_arena.allocate<EditorProjectContext>();
  *ctx->m_project = {
      .m_directory = project_directory.copy(&ctx->m_project_arena),
  };
  ctx->m_state = EditorState::Project;
  return true;
}

bool open_project(NotNull<EditorContext *> ctx, Path path) {
  if (not path.exists().value_or(false)) {
    ctx->m_ui.m_open_project.m_error =
        format(&ctx->m_popup_arena, "Failed to open {}", path);
    return false;
  }
  ctx->m_project = ctx->m_project_arena.allocate<EditorProjectContext>();
  *ctx->m_project = {
      .m_directory = path.parent().copy(&ctx->m_project_arena),
  };
  ctx->m_state = EditorState::Project;
  return true;
}

void close_project(NotNull<EditorContext *> ctx) {
  ctx->m_state = EditorState::Startup;
  ctx->m_project = nullptr;
  ctx->m_project_arena.clear();
}

struct MetaMesh {
  String8 name;
  u32 id = 0;
  Guid64 guid;
};

struct MetaEntity {
  String8 name;
  u32 id = 0;
  Span<const MetaMesh> meshes;
};

struct MetaScene {
  String8 scene;
  Span<const MetaEntity> entities;
};

JsonValue to_json(NotNull<Arena *> arena, MetaScene scene) {
  DynamicArray<JsonKeyValue> scene_json;
  scene_json.push(arena, {"scene", JsonValue::init(arena, scene.scene)});
  Span<JsonValue> json_entities =
      Span<JsonValue>::allocate(arena, scene.entities.m_size);
  for (usize entity_index : range(scene.entities.m_size)) {
    MetaEntity entity = scene.entities[entity_index];
    DynamicArray<JsonKeyValue> json_entity;
    json_entity.push(arena, {"name", JsonValue::init(arena, entity.name)});
    json_entity.push(arena, {"id", JsonValue::init(entity.id)});
    auto json_meshes = Span<JsonValue>::allocate(arena, entity.meshes.m_size);
    for (usize mesh_index : range(entity.meshes.m_size)) {
      MetaMesh mesh = entity.meshes[mesh_index];
      DynamicArray<JsonKeyValue> json_mesh;
      json_mesh.push(arena, {"name", JsonValue::init(arena, mesh.name)});
      json_mesh.push(arena, {"id", JsonValue::init(mesh.id)});
      json_mesh.push(arena,
                     {"guid", JsonValue::init(to_string(arena, mesh.guid))});
      json_meshes[mesh_index] = JsonValue::init(json_mesh);
    }
    json_entity.push(arena, {"meshes", JsonValue::init(json_meshes)});
    json_entities[entity_index] = JsonValue::init(json_entity);
  }
  scene_json.push(arena, {"entities", JsonValue::init(json_entities)});
  return JsonValue::init(scene_json);
}

MetaScene generate_mesh_metadata(NotNull<Arena *> arena, JsonValue gltf,
                                 Path filename) {
  ScratchArena scratch;
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);

  Path stem = filename.stem();

  Span<const JsonValue> meshes = json_array_value(gltf, "meshes");
  auto meta_entities = Span<MetaEntity>::allocate(arena, meshes.m_size);
  for (usize entity_index : range(meshes.m_size)) {
    String8 entity_name =
        json_string_value_or(meshes[entity_index], "name",
                             format(scratch, "{}", entity_index))
            .copy(arena);
    Span<const JsonValue> primitives =
        json_array_value(meshes[entity_index], "primitives");
    auto meta_meshes = Span<MetaMesh>::allocate(arena, primitives.m_size);
    for (usize mesh_index : range(primitives.m_size)) {
      String8 mesh_name = format(arena, "{}", mesh_index);
      blake3_hasher_reset(&hasher);
      String8 guid_src =
          String8::join(scratch, {stem.m_str, entity_name, mesh_name}, "::");
      blake3_hasher_update(&hasher, guid_src.m_str, guid_src.m_size);
      Guid64 guid;
      blake3_hasher_finalize(&hasher, guid.m_data, sizeof(guid));
      meta_meshes[mesh_index] = {
          .name = mesh_name,
          .id = (u32)mesh_index,
          .guid = guid,
      };
    }
    meta_entities[entity_index] = {
        .name = entity_name,
        .id = (u32)entity_index,
        .meshes = meta_meshes,
    };
  }

  return {
      .scene = stem.m_str.copy(arena),
      .entities = meta_entities,
  };
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

Result<void, String8> import_scene(NotNull<EditorContext *> ctx, Path path) {
  ScratchArena scratch;

  Path scene_directory =
      ctx->m_project->m_directory.concat(scratch, {ASSET_DIR, SCENE_DIR});
  Path filename = path.filename();
  Path mesh_filename = filename.replace_extension(scratch, Path::init(".gltf"));
  Path bin_filename = filename.replace_extension(scratch, Path::init(".bin"));
  Path meta_filename = filename.replace_extension(scratch, Path::init(".json"));
  Path gltf_path = scene_directory.concat(scratch, mesh_filename);
  Path bin_path = scene_directory.concat(scratch, bin_filename);
  Path meta_path = scene_directory.concat(scratch, meta_filename);
  Path blob_directory =
      ctx->m_project->m_directory.concat(scratch, {CONTENT_DIR, MESH_DIR});

  if (IoResult<void> result = create_directories(scene_directory); !result) {
    return format(&ctx->m_popup_arena, "Failed to create {}: {}",
                  scene_directory, result.error());
  }
  if (IoResult<void> result = create_directories(blob_directory); !result) {
    return format(&ctx->m_popup_arena, "Failed to create {}: {}",
                  blob_directory, result.error());
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
    return String8::init(&ctx->m_popup_arena, importer.GetErrorString());
  }
  ren_assert(scene->mNumMeshes > 1);

  Assimp::Exporter exporter;
  Assimp::ExportProperties exporter_properties;
  exporter_properties.SetPropertyString(
      AI_CONFIG_EXPORT_BLOB_NAME,
      filename.stem().m_str.zero_terminated(scratch));
  const aiExportDataBlob *blob =
      exporter.ExportToBlob(scene, "gltf2", 0, &exporter_properties);
  if (!blob) {
    return String8::init(&ctx->m_popup_arena, exporter.GetErrorString());
  }
  const aiExportDataBlob *bin = blob->next;

  if (auto result = write(gltf_path, blob->data, blob->size); !result) {
    return format(&ctx->m_popup_arena, "Failed to write {}: {}", gltf_path,
                  result.error());
  }
  if (auto result = write(bin_path, bin->data, bin->size); !result) {
    return format(&ctx->m_popup_arena, "Failed to write {}: {}", bin_path,
                  result.error());
  }

  Result<JsonValue, JsonErrorInfo> gltf =
      json_parse(scratch, String8((const char *)blob->data, blob->size));
  if (!gltf) {
    JsonErrorInfo error = gltf.error();
    return format(&ctx->m_popup_arena, "Failed to parse glTF:\n{}:{}:{}: {}",
                  gltf_path, error.line + 1, error.column + 1, error.error);
  }

  MetaScene meta = generate_mesh_metadata(scratch, *gltf, mesh_filename);
  if (auto result =
          write(meta_path, json_serialize(scratch, to_json(scratch, meta)));
      !result) {
    return format(&ctx->m_popup_arena, "Failed to write {}: {}", meta_path,
                  result.error());
  }

  for (usize entity_index : range(meta.entities.m_size)) {
    MetaEntity meta_entity = meta.entities[entity_index];
    for (usize mesh_index : range(meta_entity.meshes.m_size)) {
      MetaMesh meta_mesh = meta_entity.meshes[mesh_index];
      ScratchArena scratch;
      Span<const std::byte> blob = process_mesh(
          scratch, *gltf, {(const std::byte *)bin->data, bin->size},
          entity_index, mesh_index);
      Path blob_filename = Path::init(to_string(scratch, meta_mesh.guid));
      Path blob_path = blob_directory.concat(scratch, blob_filename);
      if (auto result = write(blob_path, blob); !result) {
        return format(&ctx->m_popup_arena, "Failed to write {}: {}", blob_path,
                      result.error());
      }
    }
  }

  return {};
}

void draw_editor_ui(NotNull<EditorContext *> ctx) {
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGui::PushFont(ctx->m_ui.m_font);

  EditorPopupMenu open_popup = EditorPopupMenu::None;

#if 0
  bool open = true;
  ImGui::ShowDemoWindow(&open);
#endif
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        open_popup = EditorPopupMenu::NewProject;
      }

      if (ImGui::MenuItem("Open...")) {
        OpenProjectUI &ui = ctx->m_ui.m_open_project;
        ui = {};
        if (!ctx->m_ui.m_open_project_dialog_properties) {
          ctx->m_ui.m_open_project_dialog_properties = SDL_CreateProperties();
          SDL_SetPointerProperty(ctx->m_ui.m_open_project_dialog_properties,
                                 SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                                 ctx->m_window);
          SDL_SetBooleanProperty(ctx->m_ui.m_open_project_dialog_properties,
                                 SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
          SDL_SetStringProperty(ctx->m_ui.m_open_project_dialog_properties,
                                SDL_PROP_FILE_DIALOG_TITLE_STRING,
                                "Open Project");
        }
        open_file_dialog(
            ctx,
            {
                .client = EditorDialogClient::OpenProject,
                .type = SDL_FILEDIALOG_OPENFILE,
                .properties = ctx->m_ui.m_open_project_dialog_properties,
                .filters = {{.name = "Ren Project Files", .pattern = "json"}},
            });
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

    if (ctx->m_state == EditorState::Project) {
      if (ImGui::BeginMenu("Import")) {
        if (ImGui::MenuItem("Mesh...")) {
          open_popup = EditorPopupMenu::ImportScene;
        }
        ImGui::EndMenu();
      }
    }

    ImGui::EndMainMenuBar();
  }

  if (ctx->m_ui.m_dialog_client == EditorDialogClient::OpenProject) {
    ScratchArena scratch;
    Path path = check_dialog_path(scratch, ctx);
    if (path) {
      close_project(ctx);
      if (!open_project(ctx, path)) {
        ImGui::OpenPopup("##Open Project Failed");
      }
    }
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("##Open Project Failed", nullptr,
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

  if (open_popup == EditorPopupMenu::NewProject) {
    ImGui::OpenPopup("New Project");
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("New Project", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    NewProjectUI &ui = ctx->m_ui.m_new_project;
    if (ImGui::IsWindowAppearing()) {
      ui = {};
      const char DEFAULT_TITLE[] = "New Project";
      ui.m_title_buffer.push(&ctx->m_popup_arena, DEFAULT_TITLE,
                             sizeof(DEFAULT_TITLE));

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

      ui.m_location_buffer.push(&ctx->m_popup_arena,
                                default_location.m_str.m_str,
                                default_location.m_str.m_size);
      ui.m_location_buffer.push(&ctx->m_popup_arena, 0);
    }

    ImGui::Text("Title:");
    InputText("##Title", &ctx->m_popup_arena, &ui.m_title_buffer);

    if (!ctx->m_ui.m_new_project_dialog_properties) {
      ctx->m_ui.m_new_project_dialog_properties = SDL_CreateProperties();
      SDL_SetPointerProperty(ctx->m_ui.m_new_project_dialog_properties,
                             SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                             ctx->m_window);
      SDL_SetBooleanProperty(ctx->m_ui.m_new_project_dialog_properties,
                             SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
      SDL_SetStringProperty(ctx->m_ui.m_new_project_dialog_properties,
                            SDL_PROP_FILE_DIALOG_TITLE_STRING,
                            "New Project Location");
    }
    InputPath("Location", ctx, &ui.m_location_buffer, SDL_FILEDIALOG_OPENFOLDER,
              ctx->m_ui.m_new_project_dialog_properties);

    ScratchArena scratch;
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
    ImGui::BeginDisabled(ctx->m_ui.m_dialog_active);
    if (ImGui::Button("Create")) {
      close_project(ctx);
      if (new_project(ctx, path)) {
        close = true;
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

  if (open_popup == EditorPopupMenu::ImportScene) {
    ImGui::OpenPopup("Import Scene");
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Import Scene", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ScratchArena scratch;
    ImportSceneUI &ui = ctx->m_ui.m_import_scene;
    if (ImGui::IsWindowAppearing()) {
      ui = {};
      ui.m_path_buffer.push(&ctx->m_popup_arena, 0);
    }

    if (!ctx->m_ui.m_import_scene_dialog_properties) {
      ctx->m_ui.m_import_scene_dialog_properties = SDL_CreateProperties();
      SDL_SetPointerProperty(ctx->m_ui.m_import_scene_dialog_properties,
                             SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                             ctx->m_window);
      SDL_SetBooleanProperty(ctx->m_ui.m_import_scene_dialog_properties,
                             SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
      SDL_SetStringProperty(ctx->m_ui.m_import_scene_dialog_properties,
                            SDL_PROP_FILE_DIALOG_TITLE_STRING,
                            "Import Scene Path");
    }
    InputPath("Path", ctx, &ui.m_path_buffer, SDL_FILEDIALOG_OPENFILE,
              ctx->m_ui.m_import_scene_dialog_properties);

    if (ui.m_import_error) {
      ImGui::Text("Import failed:\n%.*s", (int)ui.m_import_error.m_size,
                  ui.m_import_error.m_str);
    }

    bool close = false;
    ImGui::BeginDisabled(ctx->m_ui.m_dialog_active);
    if (ImGui::Button("Import")) {
      Result<void, String8> result = import_scene(
          ctx, Path::init(scratch, String8::init(ui.m_path_buffer.m_data)));
      if (result) {
        close = true;
      } else {
        ui.m_import_error = result.error();
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

    draw_editor_ui(ctx);

    ren::draw(ctx->m_scene, {.delta_time = dt_ns / 1e9f});
  } // namespace ren
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
