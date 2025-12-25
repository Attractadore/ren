#include "Assets.hpp"
#include "Editor.hpp"
#include "ren/core/Format.hpp"
#include "ren/ren.hpp"

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <fmt/base.h>
#include <tracy/Tracy.hpp>

/// 1. For scenes we need (relatively) fast insertion + (relatively) fast
/// deletion by filename. Scenes also need to be sortable for display in the UI.
/// This needs to be done only once when sort settings or contents change
/// though, and can later be reused.
/// 2. For meshes we need fast insertion, fast deletion, fast insertion into the
/// dirty list by guid, for removal from dirty list by guid, fast access by
/// guid for cross-referencing in the UI.
/// This means that for both cases we need to map a hash to a Handle. For
/// scenes a hash can be generated from the file name.

namespace ren {

void register_all_assets(NotNull<EditorContext *> ctx) {
  register_all_gltf_scenes(ctx);
}

void unregister_all_assets(NotNull<EditorContext *> ctx) {
  unregister_all_gltf_scenes(ctx);
}

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
    ScratchArena scratch;

    String8 guid_str = to_string(scratch, meta_mesh.guid);
    Path mesh_path = content.concat(scratch, Path::init(guid_str));
    u64 mtime = last_write_time(mesh_path).value_or(0);

    Handle<Mesh> gfx_handle;
    {
      IoResult<Span<char>> buffer = read(scratch, mesh_path);
      if (!buffer and buffer.error() != IoError::NotFound) {
        fmt::println(stderr, "Failed to open {}: {}", mesh_path,
                     buffer.error());
      } else if (buffer) {
        gfx_handle =
            create_mesh(&ctx->m_frame_arena, ctx->m_scene, buffer->as_bytes());
      }
    }

    first_mesh_handle = project->m_meshes.insert(
        &ctx->m_project_arena,
        {
            .guid = meta_mesh.guid,
            .name = meta_mesh.name.copy(&ctx->m_project_arena),
            .next = first_mesh_handle,
            .gfx_handle = gfx_handle,
            .is_dirty = mtime < max({gltf_mtime, bin_mtime, meta_mtime}),
        });
  }
  project->m_gltf_scenes.insert(
      &ctx->m_project_arena,
      {
          // FIXME: file names are leaked when a file is unregistered.
          .bin_filename = bin_filename.copy(&ctx->m_project_arena),
          .gltf_filename = gltf_filename.copy(&ctx->m_project_arena),
          .meta_filename = meta_filename.copy(&ctx->m_project_arena),
          .first_mesh = first_mesh_handle,
      });
}

void register_gltf_scene(NotNull<EditorContext *> ctx, Path meta_filename) {
  ren_assert(not meta_filename.is_absolute());
  ScratchArena scratch;
  Path meta_path = ctx->m_project->m_directory.concat(
      scratch, {ASSET_DIR, GLTF_DIR, meta_filename});
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
        destroy_mesh(ctx->m_scene, mesh.gfx_handle);
        Handle<EditorMesh> next = mesh.next;
        project->m_meshes.erase(mesh_handle);
        mesh_handle = next;
      }
      project->m_gltf_scenes.erase(handle);
      return;
    }
  }
}

void register_all_gltf_scenes(NotNull<EditorContext *> ctx) {
  ZoneScoped;
  ScratchArena scratch;
  Path assets =
      ctx->m_project->m_directory.concat(scratch, {ASSET_DIR, GLTF_DIR});
  IoResult<NotNull<Directory *>> dirit = open_directory(scratch, assets);
  if (!dirit) {
    fmt::println(stderr, "Failed to open {}: {}", assets, dirit.error());
    return;
  }
  while (true) {
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
    register_gltf_scene(ctx, entry);
  }
  close_directory(*dirit);
}

void unregister_all_gltf_scenes(NotNull<EditorContext *> ctx) {
  EditorProjectContext *project = ctx->m_project;
  project->m_gltf_scenes.clear();
  project->m_meshes.clear();
}

void register_all_content(NotNull<EditorContext *> ctx) {
  register_all_mesh_content(ctx);
}

void unregister_all_content(NotNull<EditorContext *> ctx) {
  unregister_all_mesh_content(ctx);
}

void register_all_mesh_content(NotNull<EditorContext *> ctx) {
  ScratchArena scratch;
  Path mesh_content_path =
      ctx->m_project->m_directory.concat(scratch, {CONTENT_DIR, MESH_DIR});
  IoResult<NotNull<Directory *>> dirit =
      open_directory(scratch, mesh_content_path);
  if (!dirit) {
    fmt::println(stderr, "Failed to open {}: {}", mesh_content_path,
                 dirit.error());
    return;
  }
  while (true) {
    ScratchArena scratch;
    IoResult<Path> entry_result = read_directory(scratch, *dirit);
    if (!entry_result) {
      fmt::println(stderr, "Failed to read directory entry in {}: {}",
                   mesh_content_path, entry_result.error());
      close_directory(*dirit);
      break;
    }
    Path entry = *entry_result;
    if (!entry) {
      break;
    }
    Optional<Guid64> guid = guid_from_string<sizeof(Guid64)>(entry);
    if (!guid) {
      continue;
    }
    register_mesh_content(ctx, *guid);
  }
  close_directory(*dirit);
}

void unregister_all_mesh_content(NotNull<EditorContext *> ctx) {
  for (auto &&[_, mesh] : ctx->m_project->m_meshes) {
    mesh.is_dirty = true;
  }
}

void register_mesh_content(NotNull<EditorContext *> ctx, Guid64 guid) {
  for (auto &&[_, mesh] : ctx->m_project->m_meshes) {
    if (mesh.guid == guid) {
      ScratchArena scratch;
      Path mesh_path = ctx->m_project->m_directory.concat(
          scratch,
          {CONTENT_DIR, MESH_DIR, Path::init(to_string(scratch, guid))});
      Handle<Mesh> gfx_handle;
      IoResult<Span<char>> buffer = read(scratch, mesh_path);
      if (!buffer) {
        fmt::println(stderr, "Failed to open {}: {}", mesh_path,
                     buffer.error());
      } else {
        gfx_handle =
            create_mesh(&ctx->m_frame_arena, ctx->m_scene, buffer->as_bytes());
      }
      if (gfx_handle) {
        destroy_mesh(ctx->m_scene, mesh.gfx_handle);
        mesh.gfx_handle = gfx_handle;
      }
      mesh.is_dirty = false;
      break;
    }
  }
}

void unregister_mesh_content(NotNull<EditorContext *> ctx, Guid64 guid) {
  for (auto &&[_, mesh] : ctx->m_project->m_meshes) {
    if (mesh.guid == guid) {
      mesh.is_dirty = true;
      break;
    }
  }
}

JobFuture<Result<void, String8>> job_import_scene(NotNull<EditorContext *> ctx,
                                                  ArenaTag tag, Path path) {
  JobFuture<Result<void, String8>> future = job_dispatch(
      tag, "Import Scene", [ctx, tag, path]() -> Result<void, String8> {
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

} // namespace ren
