#include "AssetWatcher.hpp"
#include "Editor.hpp"
#include "ren/core/FileWatcher.hpp"
#include "ren/core/Format.hpp"

#include <fmt/base.h>
#include <tracy/Tracy.hpp>

namespace ren {

u64 EVENT_REPORT_DELAY_NS = 1'000'000'000;

void start_asset_watcher(NotNull<EditorContext *> ctx) {
  ZoneScoped;
  EditorProjectContext *project = ctx->m_project;
  Path root = project->m_directory;
  project->m_asset_watcher =
      start_file_watcher(&ctx->m_project_arena, root, EVENT_REPORT_DELAY_NS);
  if (!project->m_asset_watcher) {
    return;
  }
  ScratchArena scratch;
  Path assets_relative_path = ASSET_DIR;
  Path gltf_relative_path = assets_relative_path.concat(scratch, GLTF_DIR);
  Path content_relative_path = CONTENT_DIR;
  Path mesh_relative_path = content_relative_path.concat(scratch, MESH_DIR);
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  Path::init("."));
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  assets_relative_path);
  watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                  gltf_relative_path);
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

  Path assets_relative_path = ASSET_DIR;
  Path gltf_relative_path = assets_relative_path.concat(scratch, GLTF_DIR);
  Path content_relative_path = CONTENT_DIR;
  Path mesh_content_relative_path =
      content_relative_path.concat(scratch, MESH_DIR);
  while (true) {
    Optional<FileWatchEvent> event =
        read_watch_event(scratch, project->m_asset_watcher);
    if (!event) {
      return;
    }

    if (event->type == FileWatchEventType::QueueOverflow) {
      stop_asset_watcher(ctx);
      unregister_all_assets(ctx);
      start_asset_watcher(ctx);
      register_all_assets(ctx);
    }

    if (event->type == FileWatchEventType::Other) {
      continue;
    }

    bool is_fuzzy = event->type == FileWatchEventType::Fuzzy;
    bool is_delete = event->type == FileWatchEventType::Removed or
                     event->type == FileWatchEventType::RenamedFrom;
    bool is_modify = event->type == FileWatchEventType::RenamedTo or
                     event->type == FileWatchEventType::Modified;
    Path relative_path = event->type == FileWatchEventType::Fuzzy
                             ? event->parent
                             : event->parent.concat(scratch, event->filename);

    if (relative_path == gltf_relative_path) {
      if (is_delete or is_fuzzy) {
        unregister_all_gltf_scenes(ctx);
        watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                        gltf_relative_path);
        register_all_gltf_scenes(ctx);
      }
      continue;
    }

    if (relative_path == mesh_content_relative_path) {
      if (is_delete or is_fuzzy) {
        unregister_all_mesh_content(ctx);
        watch_directory(&ctx->m_project_arena, project->m_asset_watcher,
                        mesh_content_relative_path);
        register_all_mesh_content(ctx);
      }
      continue;
    }

    if (relative_path == assets_relative_path or
        relative_path == content_relative_path) {
      if (is_delete or is_fuzzy) {
        stop_asset_watcher(ctx);
        unregister_all_assets(ctx);
        start_asset_watcher(ctx);
        register_all_assets(ctx);
      }
      continue;
    }

    if (is_fuzzy) {
      fmt::println(stderr, "AssetWatcher: unhandled fuzzy event for {}",
                   event->parent);
      continue;
    }

    if (event->parent == gltf_relative_path) {
      if (event->filename.extension() == META_EXT) {
        if (is_modify) {
          unregister_gltf_scene(ctx, event->filename);
          register_gltf_scene(ctx, event->filename);
        } else if (is_delete) {
          unregister_gltf_scene(ctx, event->filename);
        }
        continue;
      }

      if (event->filename.extension() == ".gltf" or
          event->filename.extension() == ".bin") {
        if (is_modify) {
          Path meta_filename =
              event->filename.replace_extension(scratch, ".gltf")
                  .add_extension(scratch, META_EXT);
          unregister_gltf_scene(ctx, meta_filename);
          register_gltf_scene(ctx, meta_filename);
        }
        continue;
      }

      continue;
    }

    if (event->parent == mesh_content_relative_path) {
      Optional<Guid64> guid = guid_from_string<sizeof(Guid64)>(event->filename);
      if (!guid) {
        continue;
      }
      if (is_modify) {
        unregister_mesh_content(ctx, *guid);
        register_mesh_content(ctx, *guid);
      } else if (is_delete) {
        unregister_mesh_content(ctx, *guid);
      }
      continue;
    }
  }
}

} // namespace ren
