#pragma once
#include "ren/core/Array.hpp"
#include "ren/core/GenIndex.hpp"
#include "ren/core/Job.hpp"
#include "ren/core/Result.hpp"
#include "ren/core/String.hpp"

struct ImFont;

namespace ren {

struct EditorContext;
struct EditorSceneNode;

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
  JobFuture<Result<void, String8>> m_import_future;
  String8 m_import_error;
};

struct AssetCompilationUI {
  bool was_done = false;
  Span<String8> compilation_errors;
};

struct SceneHierarchyUI {
  Handle<EditorSceneNode> edit_node;
  DynamicArray<char> edit_buffer;
};

struct EditorUI {
  ImFont *m_font = nullptr;
  NewProjectUI m_new_project;
  OpenProjectUI m_open_project;
  ImportSceneUI m_import_scene;
  AssetCompilationUI m_asset_compilation;
  SceneHierarchyUI m_scene_hierarchy;
};

void load_recently_opened_list(NotNull<EditorContext *> ctx);
void save_recently_opened_list(NotNull<EditorContext *> ctx);

void draw_editor_ui(NotNull<EditorContext *> ctx);

} // namespace ren
