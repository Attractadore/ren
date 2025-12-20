#include "Assets.hpp"
#include "Editor.hpp"
#include "ren/core/Format.hpp"

#include "ren/core/glTF.hpp"
#include <fmt/base.h>

namespace ren {

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

/// 1. For scenes we need (relatively) fast insertion + (relatively) fast
/// deletion by filename. Scenes also need to be sortable for display in the UI.
/// This needs to be done only once when sort settings or contents change
/// though, and can later be reused.
/// 2. For meshes we need fast insertion, fast deletion, fast insertion into the
/// dirty list by guid, for removal from dirty list by guid, fast access by
/// guid for cross-referencing in the UI.
/// This means that for both cases we need to map a hash to a Handle. For
/// scenes a hash can be generated from the file name.

void unregister_gltf_scene(NotNull<EditorContext *> ctx, Path meta_filename) {
  // TODO: fix linear search, it's slow.
  EditorProjectContext *project = ctx->m_project;
  for (auto &&[handle, gltf_scene] : project->m_gltf_scenes) {
    if (gltf_scene.meta_filename == meta_filename) {
      Handle<EditorMesh> mesh_handle = gltf_scene.first_mesh;
      while (mesh_handle) {
        const EditorMesh &mesh = project->m_meshes[mesh_handle];
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
}

void mark_gltf_scene_dirty(NotNull<EditorContext *> ctx, Path meta_filename) {
  // TODO: fix linear search, it's slow.
  EditorProjectContext *project = ctx->m_project;
  for (auto &&[_, gltf_scene] : project->m_gltf_scenes) {
    if (gltf_scene.meta_filename == meta_filename) {
      Handle<EditorMesh> mesh_handle = gltf_scene.first_mesh;
      while (mesh_handle) {
        EditorMesh &mesh = project->m_meshes[mesh_handle];
        mesh.is_dirty = true;
        mesh_handle = mesh.next;
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
        EditorMesh &mesh = project->m_meshes[mesh_handle];
        mesh.is_dirty = false;
        mesh_handle = mesh.next;
      }
      return;
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

        Result<Gltf, GltfErrorInfo> gltf_result =
            gltf_parse_file(scratch, path);
        if (!gltf_result) {
          GltfErrorInfo error_info = gltf_result.error();
          switch (error_info.error) {
          case GltfError::InvalidFormat: {
            return error_info.desc.copy(&output);
          }
          case GltfError::IO: {
            return error_info.desc.copy(&output);
          }
          default:
            break;
          }
        }

        ren_assert(gltf_result->meshes.m_size > 1);

        auto get_component_size = [](GltfComponentType type) -> i32 {
          switch (type) {
          case GLTF_COMPONENT_TYPE_BYTE:
          case GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return 1;
          case GLTF_COMPONENT_TYPE_SHORT:
          case GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return 2;
          case GLTF_COMPONENT_TYPE_UNSIGNED_INT:
          case GLTF_COMPONENT_TYPE_FLOAT:
            return 4;
          default:
            return 0;
          }
        };

        auto get_type_count = [](GltfType type) -> i32 {
          switch (type) {
          case GLTF_TYPE_SCALAR:
            return 1;
          case GLTF_TYPE_VEC2:
            return 2;
          case GLTF_TYPE_VEC3:
            return 3;
          case GLTF_TYPE_VEC4:
            return 4;
          case GLTF_TYPE_MAT2:
            return 4;
          case GLTF_TYPE_MAT3:
            return 9;
          case GLTF_TYPE_MAT4:
            return 16;
          default:
            return 0;
          }
        };

        auto primitives_match = [&](const GltfPrimitive &a,
                                    const GltfPrimitive &b) -> bool {
          if (a.indices != b.indices)
            return false;
          if (a.attributes.m_size != b.attributes.m_size)
            return false;
          if (a.mode != b.mode)
            return false;

          for (u32 i = 0; i < a.attributes.m_size; ++i) {
            bool found = false;
            for (u32 j = 0; j < b.attributes.m_size; ++j) {
              if (a.attributes[i].name == b.attributes[j].name &&
                  a.attributes[i].accessor == b.attributes[j].accessor) {
                found = true;
                break;
              }
            }
            if (!found)
              return false;
          }
          return true;
        };

        DynamicArray<i32> mesh_mapping;
        mesh_mapping.reserve(scratch, gltf_result->meshes.m_size);

        DynamicArray<i32> unique_mesh_indices;
        unique_mesh_indices.reserve(scratch, gltf_result->meshes.m_size);

        for (u32 i = 0; i < gltf_result->meshes.m_size; ++i) {
          const GltfMesh &mesh = gltf_result->meshes[i];

          i32 found_index = -1;
          for (u32 j = 0; j < unique_mesh_indices.m_size; ++j) {
            const GltfMesh &unique_mesh =
                gltf_result->meshes[unique_mesh_indices[j]];

            if (mesh.primitives.m_size == unique_mesh.primitives.m_size) {
              bool all_match = true;
              for (u32 p = 0; p < mesh.primitives.m_size; ++p) {
                if (!primitives_match(mesh.primitives[p],
                                      unique_mesh.primitives[p])) {
                  all_match = false;
                  break;
                }
              }
              if (all_match) {
                found_index = j;
                break;
              }
            }
          }

          if (found_index == -1) {
            mesh_mapping.push(scratch, unique_mesh_indices.m_size);
            unique_mesh_indices.push(scratch, i);
          } else {
            mesh_mapping.push(scratch, found_index);
          }
        }

        i32 whole_buffer_size = 0;
        for (const GltfAccessor &accessor : gltf_result->accessors) {
          const GltfBufferView &buffer_view =
              gltf_result->buffer_views[accessor.buffer_view];

          i32 component_size = get_component_size(accessor.component_type);
          i32 type_count = get_type_count(accessor.type);
          i32 element_size = component_size * type_count;
          i32 stride = buffer_view.byte_stride > 0 ? buffer_view.byte_stride
                                                   : element_size;
          i32 data_size = stride * (accessor.count - 1) + element_size;

          whole_buffer_size += data_size;
        }

        Gltf scene = {};
        scene.asset = gltf_result->asset;
        scene.scene = gltf_result->scene;
        scene.scenes = gltf_result->scenes;

        scene.nodes.reserve(scratch, gltf_result->nodes.m_size);
        for (const GltfNode &node : gltf_result->nodes) {
          GltfNode new_node = node;
          if (node.mesh != -1) {
            new_node.mesh = mesh_mapping[node.mesh];
          }
          scene.nodes.push(scratch, new_node);
        }

        GltfBuffer new_buffer = {
            .name = filename.replace_extension(scratch, Path::init("")).m_str,
            .uri = bin_filename.m_str,
            .data = Span<u8>::allocate(scratch, whole_buffer_size)};
        scene.buffers.push(scratch, new_buffer);

        DynamicArray<i32> accessor_mapping;
        accessor_mapping.reserve(scratch, gltf_result->accessors.m_size);
        for (u32 i = 0; i < gltf_result->accessors.m_size; ++i) {
          accessor_mapping.push(scratch, -1);
        }

        i32 buffer_offset = 0;

        for (u32 mesh_idx : unique_mesh_indices) {
          const GltfMesh &mesh = gltf_result->meshes[mesh_idx];
          GltfMesh new_mesh = {.name = mesh.name};

          for (const GltfPrimitive &primitive : mesh.primitives) {
            GltfPrimitive new_primitive = {.mode = primitive.mode};

            for (const GltfAttribute &attribute : primitive.attributes) {
              i32 old_accessor_idx = attribute.accessor;

              if (accessor_mapping[old_accessor_idx] == -1) {
                const GltfAccessor &accessor =
                    gltf_result->accessors[old_accessor_idx];
                const GltfBufferView &buffer_view =
                    gltf_result->buffer_views[accessor.buffer_view];
                const GltfBuffer &buffer =
                    gltf_result->buffers[buffer_view.buffer];

                i32 component_size =
                    get_component_size(accessor.component_type);
                i32 type_count = get_type_count(accessor.type);
                i32 element_size = component_size * type_count;
                i32 stride = buffer_view.byte_stride > 0
                                 ? buffer_view.byte_stride
                                 : element_size;
                i32 data_size = stride * (accessor.count - 1) + element_size;

                i32 src_offset =
                    buffer_view.byte_offset + accessor.buffer_offset;
                const u8 *src_data_ptr = &buffer.data[src_offset];
                u8 *dst_data_ptr = &new_buffer.data[buffer_offset];
                std::memcpy(dst_data_ptr, src_data_ptr, data_size);

                GltfBufferView new_buffer_view = {.name = buffer_view.name,
                                                  .buffer = 0,
                                                  .byte_offset = buffer_offset,
                                                  .byte_length = data_size,
                                                  .byte_stride =
                                                      buffer_view.byte_stride,
                                                  .target = buffer_view.target};
                scene.buffer_views.push(scratch, new_buffer_view);

                GltfAccessor new_accessor = {
                    .name = accessor.name,
                    .buffer_view = (i32)scene.buffer_views.m_size - 1,
                    .buffer_offset = 0,
                    .component_type = accessor.component_type,
                    .normalized = accessor.normalized,
                    .count = accessor.count,
                    .type = accessor.type};
                std::memcpy(&new_accessor.min, accessor.min,
                            sizeof(new_accessor.min));
                std::memcpy(&new_accessor.max, accessor.max,
                            sizeof(new_accessor.max));
                scene.accessors.push(scratch, new_accessor);

                accessor_mapping[old_accessor_idx] =
                    (i32)scene.accessors.m_size - 1;
                buffer_offset += data_size;
              }

              GltfAttribute new_attribute = {
                  .name = attribute.name,
                  .accessor = accessor_mapping[old_accessor_idx]};
              new_primitive.attributes.push(scratch, new_attribute);
            }

            if (primitive.indices != -1) {
              i32 old_accessor_idx = primitive.indices;

              if (accessor_mapping[old_accessor_idx] == -1) {
                const GltfAccessor &accessor =
                    gltf_result->accessors[old_accessor_idx];
                const GltfBufferView &buffer_view =
                    gltf_result->buffer_views[accessor.buffer_view];
                const GltfBuffer &buffer =
                    gltf_result->buffers[buffer_view.buffer];

                i32 component_size =
                    get_component_size(accessor.component_type);
                i32 type_count = get_type_count(accessor.type);
                i32 element_size = component_size * type_count;
                i32 stride = buffer_view.byte_stride > 0
                                 ? buffer_view.byte_stride
                                 : element_size;
                i32 data_size = stride * (accessor.count - 1) + element_size;

                i32 src_offset =
                    buffer_view.byte_offset + accessor.buffer_offset;
                const u8 *src_data_ptr = &buffer.data[src_offset];
                u8 *dst_data_ptr = &new_buffer.data[buffer_offset];
                std::memcpy(dst_data_ptr, src_data_ptr, data_size);

                GltfBufferView new_buffer_view = {.name = buffer_view.name,
                                                  .buffer = 0,
                                                  .byte_offset = buffer_offset,
                                                  .byte_length = data_size,
                                                  .byte_stride =
                                                      buffer_view.byte_stride,
                                                  .target = buffer_view.target};
                scene.buffer_views.push(scratch, new_buffer_view);

                GltfAccessor new_accessor = {
                    .name = accessor.name,
                    .buffer_view = (i32)scene.buffer_views.m_size - 1,
                    .buffer_offset = 0,
                    .component_type = accessor.component_type,
                    .normalized = accessor.normalized,
                    .count = accessor.count,
                    .type = accessor.type};
                std::memcpy(&new_accessor.min, accessor.min,
                            sizeof(new_accessor.min));
                std::memcpy(&new_accessor.max, accessor.max,
                            sizeof(new_accessor.max));
                scene.accessors.push(scratch, new_accessor);

                accessor_mapping[old_accessor_idx] =
                    (i32)scene.accessors.m_size - 1;
                buffer_offset += data_size;
              }

              new_primitive.indices = accessor_mapping[old_accessor_idx];
            }

            new_mesh.primitives.push(scratch, new_primitive);
          }

          scene.meshes.push(scratch, new_mesh);
        }

        String8 new_gltf_scene_data = gltf_serialize_to_string(scratch, scene);

        if (auto result = write(gltf_path, new_gltf_scene_data.m_str,
                                new_gltf_scene_data.m_size);
            !result) {
          return format(&output, "Failed to write {}: {}", gltf_path,
                        result.error());
        }

        if (auto result = write(bin_path, scene.buffers[0].data); !result) {
          return format(&output, "Failed to write {}: {}", bin_path,
                        result.error());
        }

        MetaGltf meta =
            meta_gltf_generate(scratch, *gltf_result, gltf_filename);
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
