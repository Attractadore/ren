#pragma once
#include "AssetCompiler.hpp"
#include "Assets.hpp"
#include "EditorUI.hpp"
#include "ren/core/Arena.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/GenArray.hpp"
#include "ren/core/Job.hpp"
#include "ren/ren.hpp"

struct SDL_Window;

namespace ren {

static const Path ASSET_DIR = Path::init("assets");
static const Path GLTF_DIR = Path::init("glTF");
static const Path META_EXT = Path::init(".meta");

static const Path CONTENT_DIR = Path::init("content");
static const Path MESH_DIR = Path::init("mesh");

Path editor_settings_directory(NotNull<Arena *> arena);

Path editor_recently_opened_list_path(NotNull<Arena *> arena);

Path editor_default_project_directory(NotNull<Arena *> arena);

struct Renderer;
struct SwapChain;
struct Scene;
struct FileWatcher;

enum class EditorState {
  Startup,
  Project,
  Quit,
};

struct EditorBackgroundJob {
  JobToken token;
  ArenaTag tag;
};

struct EditorProjectContext {
  Path m_directory;
  DynamicArray<EditorBackgroundJob> m_background_jobs;

  GenArray<EditorGltfScene> m_gltf_scenes;
  GenArray<EditorMesh> m_meshes;
  Handle<EditorSceneNode> m_sceen_root;
  GenArray<EditorSceneNode> m_scene_nodes;

  FileWatcher *m_asset_watcher = nullptr;

  EditorAssetCompiler m_asset_compiler;
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
  DynamicArray<char> m_input_buffer;
};

void init_editor(int argc, const char *argv[], NotNull<EditorContext *> ctx);
void run_editor(NotNull<EditorContext *> ctx);
void quit_editor(NotNull<EditorContext *> ctx);

Result<void, String8> open_project(NotNull<EditorContext *> ctx, Path path);
void close_project(NotNull<EditorContext *> ctx);
Result<void, String8> new_project(NotNull<EditorContext *> ctx,
                                  Path project_directory);

} // namespace ren
