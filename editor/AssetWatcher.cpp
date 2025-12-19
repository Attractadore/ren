#include "AssetWatcher.hpp"
#include "Editor.hpp"
#include "ren/core/FileWatcher.hpp"

#include <tracy/Tracy.hpp>

namespace ren {

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
  Path content_relative_path = CONTENT_DIR;
  Path mesh_relative_path = content_relative_path.concat(scratch, MESH_DIR);
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  content_relative_path);
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  mesh_relative_path);
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
  Path mesh_relative_path = CONTENT_DIR.concat(scratch, MESH_DIR);
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

    if (event->type != FileWatchEventType::CreatedOrModified and
        event->type != FileWatchEventType::Removed) {
      continue;
    }

    if (event->parent == mesh_relative_path and event->filename) {
      Optional<Guid64> guid =
          guid_from_string<sizeof(Guid64)>(event->filename.m_str);
      if (!guid) {
        continue;
      }
      // FIXME: linear search is slow.
      for (auto &&[_, mesh] : project->m_meshes) {
        if (mesh.guid == *guid) {
          mesh.is_dirty = event->type == FileWatchEventType::Removed;
          break;
        }
      }
    } else if (event->parent == gltf_relative_path and event->filename) {
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

} // namespace ren
