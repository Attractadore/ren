#include "EditorUI.hpp"
#include "Editor.hpp"
#include "UIWidgets.hpp"
#include "imgui_internal.h"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Format.hpp"

#include <atomic>
#include <fmt/base.h>
#include <imgui.hpp>

namespace ren {

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

static Handle<EditorSceneNode>
add_scene_node_from_ui(NotNull<EditorContext *> ctx,
                       Handle<EditorSceneNode> parent_handle,
                       Handle<EditorSceneNode> prev_handle, String8 name) {
  Handle<EditorSceneNode> node_handle =
      add_scene_node(ctx, parent_handle, prev_handle, name);
  SceneHierarchyUI &ui = ctx->m_ui.m_scene_hierarchy;
  ui.selected_node = node_handle;
  ui.rename_node = true;
  return node_handle;
}

static void draw_scene_node_ui(NotNull<EditorContext *> ctx,
                               Handle<EditorSceneNode> node_handle) {
  ScratchArena scratch;
  EditorProjectContext *project = ctx->m_project;
  const auto &nodes = project->m_scene_nodes;
  EditorSceneNode &node = project->m_scene_nodes[node_handle];
  bool is_leaf = !node.first_child;

  SceneHierarchyUI &ui = ctx->m_ui.m_scene_hierarchy;

  auto id = std::bit_cast<ImGuiID>(node_handle);
  ImGui::SetNextItemStorageID(id);

  const char *c_str = node.name.zero_terminated(scratch);

  ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (is_leaf) {
    node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
  }
  if (node_handle != ui.edit_node) {
    node_flags |=
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_SpanFullWidth;
  }
  if (node_handle == ui.selected_node) {
    node_flags |= ImGuiTreeNodeFlags_Selected;
  }

  bool expanded = ImGui::TreeNodeEx(c_str, node_flags, "%s",
                                    node_handle == ui.edit_node ? "" : c_str);

#if 0
  static double last_click_time = 0.0;
  double current_time = ImGui::GetTime();
  const double click_detection_time = 0.3;
  static int click_count = 0;
  static Handle<EditorSceneNode> clicked_node = {};

  bool single_click = false;
  bool double_click = false;

  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    if (click_count++ == 0) {
      clicked_node = node_handle;
      last_click_time = current_time;
    } 

    if (click_count != 0 && clicked_node != node_handle) {
      click_count = 1;
      clicked_node = node_handle;
      last_click_time = current_time;
    }
  }

  if (click_count != 0) {
    if ((current_time - last_click_time) > click_detection_time) {
      single_click = click_count == 1;
      double_click = click_count == 2;
      click_count = 0;
    }
  }

  if (single_click) {
    if (clicked_node != ui.selected_node) {
      ui.selected_node = clicked_node;
    } else {
      ui.rename_node = true;
    }
  }
#else
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
      ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    ui.selected_node = node_handle;
  }
#endif

  if (node_handle == ui.selected_node && ImGui::IsKeyPressed(ImGuiKey_F2)) {
    ui.rename_node = true;
  }

  bool removed = false;
  bool force_expand = false;
  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::Button("Add child node")) {
      add_scene_node_from_ui(ctx, node_handle, node.last_child, "New node");
      force_expand = true;
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::Button("Add node before")) {
      add_scene_node_from_ui(ctx, node.parent, node.prev_sibling, "New node");
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::Button("Add node after")) {
      add_scene_node_from_ui(ctx, node.parent, node_handle, "New node");
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::Button("Rename")) {
      ui.rename_node = true;
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::Button("Remove")) {
      remove_scene_node(ctx, node_handle);
      removed = true;
      ImGui::CloseCurrentPopup();
    }

    if (ImGui::Button("Remove with children")) {
      remove_scene_node_with_children(ctx, node_handle);
      removed = true;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
  if (force_expand) {
    ImGui::TreeNodeSetOpen(id, true);
  }

  if (ui.rename_node && node_handle == ui.selected_node) {
    ui.rename_node = false;

    ui.edit_node = ui.selected_node;
    ui.edit_buffer = {};
    ui.edit_buffer.push(&ctx->m_popup_arena, node.name);
    ui.edit_buffer.push(&ctx->m_popup_arena, 0);
    // Focus input field.
    ImGui::SetKeyboardFocusHere();
  }

  if (node_handle == ui.edit_node) {
    ImGui::SameLine();
    InputText("##rename", &ctx->m_popup_arena, &ui.edit_buffer,
              ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivated()) {
      node.name = String8::init(&ctx->m_project_arena, ui.edit_buffer.data());
      ui.edit_node = NullHandle;
      ui.edit_buffer = {};
      ctx->m_popup_arena.clear();
    }
  }

  if (not is_leaf and expanded and not removed) {
    Handle<EditorSceneNode> cursor = node.first_child;
    usize id = 0;
    while (cursor) {
      Handle<EditorSceneNode> next = nodes[cursor].next_sibling;
      ImGui::PushID(id++);
      draw_scene_node_ui(ctx, cursor);
      ImGui::PopID();
      cursor = next;
    }
  }

  if (expanded) {
    ImGui::TreePop();
  }
}

static void draw_scene_hierarchy_ui(NotNull<EditorContext *> ctx) {
  if (!ImGui::BeginChild("##hierarchy", {0.0f, 0.0f}, ImGuiChildFlags_None,
                         ImGuiWindowFlags_None)) {
    ImGui::EndChild();
    return;
  }

  ScratchArena scratch;

  EditorProjectContext *project = ctx->m_project;
  const auto &nodes = project->m_scene_nodes;
  Handle<EditorSceneNode> root_handle = project->m_sceen_root;
  const EditorSceneNode &root = nodes[root_handle];

  if (ImGui::BeginPopupContextWindow()) {
    if (ImGui::Button("Add node")) {
      add_scene_node_from_ui(ctx, root_handle, root.last_child,
                             "New root node");
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  Handle<EditorSceneNode> cursor = root.first_child;
  usize id = 0;
  while (cursor) {
    Handle<EditorSceneNode> next = nodes[cursor].next_sibling;
    ImGui::PushID(id++);
    draw_scene_node_ui(ctx, cursor);
    ImGui::PopID();
    cursor = next;
  }

  ImGui::EndChild();
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
  const char *COMPILING_ASSETS_POPUP_TEXT = "Compiling Assets";
  ImGuiID compiling_assets_popup = ImGui::GetID(COMPILING_ASSETS_POPUP_TEXT);

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
      if (ImGui::BeginMenu("Assets")) {
        if (ImGui::BeginMenu("Import")) {
          if (ImGui::MenuItem("Scene")) {
            ImGui::OpenPopup(import_scene_popup);
          }
          ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Compile")) {
          launch_asset_compilation(ctx);
          ImGui::OpenPopup(compiling_assets_popup);
        }
        if (ImGui::MenuItem("Clean Compiled")) {
          ScratchArena scratch;
          Path content =
              ctx->m_project->m_directory.concat(scratch, CONTENT_DIR);
          IoResult<void> remove_result = remove_directory_tree(content);
          if (!remove_result) {
            fmt::println("Failed to remove {}: {}", content,
                         remove_result.error());
          }
        }
        if (ImGui::MenuItem("Recompile All")) {
          ScratchArena scratch;
          Path content =
              ctx->m_project->m_directory.concat(scratch, CONTENT_DIR);
          IoResult<void> remove_result = remove_directory_tree(content);
          if (!remove_result) {
            fmt::println("Failed to remove {}: {}", content,
                         remove_result.error());
          }
          launch_asset_compilation(ctx, AssetCompilationScope::All);
          ImGui::OpenPopup(compiling_assets_popup);
        }
        ImGui::EndMenu();
      }
    }
  }
  float menu_height = ImGui::GetWindowHeight();
  ImGui::EndMainMenuBar();
  const ImGuiViewport *viewport = ImGui::GetMainViewport();

  if (ctx->m_project) {
    constexpr ImGuiWindowFlags SIDE_PANEL_FLAGS =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoScrollWithMouse;
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
          draw_scene_hierarchy_ui(ctx);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Meshes", nullptr)) {
          if (ImGui::BeginChild("##tree", {0.0f, 0.0f}, ImGuiChildFlags_None,
                                ImGuiWindowFlags_None)) {
            ScratchArena scratch;
            for (const auto &[_, scene] : ctx->m_project->m_gltf_scenes) {
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
                  if (mesh.is_dirty) {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                  }
                  if (ImGui::TreeNodeEx(mesh.name.zero_terminated(scratch),
                                        ImGuiTreeNodeFlags_Leaf |
                                            ImGuiTreeNodeFlags_Bullet)) {
                    ImGui::TreePop();
                  }
                  if (mesh.is_dirty) {
                    ImGui::PopStyleColor();
                  }
                  mesh_handle = mesh.next;
                }
                ImGui::TreePop();
              }
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
      job_reset_tag(ArenaNamedTag::EditorImportScene);
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
        Path path = Path::init(&ctx->m_popup_arena,
                               String8::init(ui.m_path_buffer.m_data));
        ui.m_import_future =
            job_import_scene(ctx, ArenaNamedTag::EditorImportScene, path);
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

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(COMPILING_ASSETS_POPUP_TEXT, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    AssetCompilationUI &ui = ctx->m_ui.m_asset_compilation;
    if (ImGui::IsWindowAppearing()) {
      ui = {};
    }
    const EditorAssetCompilerSession *session =
        &ctx->m_project->m_asset_compiler.m_session;
    u32 num_finished = std::atomic_ref(session->m_num_finished_jobs)
                           .load(std::memory_order_acquire);
    u32 num_launched = session->m_num_jobs;
    bool is_done = ui.was_done or job_is_done(session->m_job);
    if (is_done and not ui.was_done) {
      ScratchArena scratch;
      DynamicArray<String8> compilation_errors;
      for (const MeshCompileJobResult &job_result :
           session->m_job_results.subspan(0, num_finished)) {
        if (job_result.error) {
          String8 name;
          // FIXME(mbargatin): linear search is slow.
          for (const auto &[_, mesh] : ctx->m_project->m_meshes) {
            if (mesh.guid == job_result.guid) {
              name = mesh.name;
              break;
            }
          }
          String8 error = format(
              &ctx->m_popup_arena, "{} ({}): {}", name ? name : "Unknown",
              to_string(scratch, job_result.guid), job_result.error);
          compilation_errors.push(&ctx->m_popup_arena, error);
        }
      }
      ui.compilation_errors = compilation_errors;
      ui.was_done = true;
      job_reset_tag(ArenaNamedTag::EditorCompile);
    }
    bool is_canceled =
        std::atomic_ref(session->m_stop_token).load(std::memory_order_relaxed);
    if (not is_done) {
      if (is_canceled) {
        ImGui::Text("Canceling...");
      } else {
        ImGui::Text("Compiling assets: %d/%d...", num_finished, num_launched);
      }
      ImGui::ProgressBar(
          num_launched > 0.0f ? (float)num_finished / num_launched : 1.0f);
    } else {
      if (ui.compilation_errors.m_size > 0) {
        ScratchArena scratch;
        ImGui::Text("Failed: compiled %d/%d assets", num_finished,
                    num_launched);
        ImGui::Text("Got %d errors:", (int)ui.compilation_errors.m_size);
        if (ImGui::BeginChild("##errors", viewport->Size * 0.3f)) {
          for (String8 error : ui.compilation_errors) {
            ImGui::TextWrapped("%s", error.zero_terminated(scratch));
          }
        }
        ImGui::EndChild();
      } else if (is_canceled) {
        ImGui::Text("Canceled: compiled %d/%d assets", num_finished,
                    num_launched);
      } else {
        ImGui::Text("Done: successfully compiled %d assets", num_launched);
      }
    }

    ImGui::BeginDisabled(is_done or is_canceled);
    if (ImGui::Button("Cancel")) {
      std::atomic_ref(ctx->m_project->m_asset_compiler.m_session.m_stop_token)
          .store(true, std::memory_order_relaxed);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(not is_done);
    if (ImGui::Button("Close")) {
      ren_assert(is_done);
      ctx->m_project->m_asset_compiler.m_session = {};
      ctx->m_popup_arena.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }

  ImGui::PopFont();
  ImGui::Render();
  ImGui::EndFrame();
}

} // namespace ren
