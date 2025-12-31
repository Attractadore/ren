#include "AssetCompiler.hpp"
#include "Editor.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/glTF.hpp"

#include <atomic>

namespace ren {

template <typename T>
Span<const T> accessor_data(Span<const char> bin, JsonValue gltf,
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
  case GLTF_COMPONENT_TYPE_BYTE:
    component_size = sizeof(i8);
    break;
  case GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    component_size = sizeof(u8);
    break;
  case GLTF_COMPONENT_TYPE_SHORT:
    component_size = sizeof(i16);
    break;
  case GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    component_size = sizeof(u16);
    break;
  case GLTF_COMPONENT_TYPE_UNSIGNED_INT:
    component_size = sizeof(u32);
    break;
  case GLTF_COMPONENT_TYPE_FLOAT:
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

Result<void, String8> compile_mesh(NotNull<Arena *> arena, Guid64 guid,
                                   Path gltf_path, Path blob_path) {
  ScratchArena scratch;

  Path bin_path = gltf_path.replace_extension(scratch, ".bin");
  Path meta_path = gltf_path.add_extension(scratch, META_EXT);

  MetaMesh meta_mesh;
  {
    IoResult<Span<char>> buffer = read(scratch, meta_path);
    if (!buffer) {
      return format(arena, "Failed to read {}: {}", meta_path, buffer.error());
    }
    Result<JsonValue, JsonErrorInfo> json =
        json_parse(scratch, {buffer->m_data, buffer->m_size});
    if (!json) {
      JsonErrorInfo error = json.error();
      return format(arena, "{}:{}:{}: {}", meta_path, error.line, error.column,
                    error.error);
    }
    Result<MetaGltf, MetaGltfErrorInfo> meta =
        meta_gltf_from_json(scratch, *json);
    if (!meta) {
      return format(arena, "Failed to parse meta file {}: ", meta_path,
                    to_string(scratch, meta.error()));
    }
    for (const MetaMesh &mesh : meta->meshes) {
      if (mesh.guid == guid) {
        meta_mesh = mesh;
        break;
      }
    }
  }
  if (!meta_mesh.guid) {
    return format(arena, "Failed to find {} in {}", to_string(scratch, guid),
                  meta_path);
  }

  JsonValue gltf;
  {
    IoResult<Span<char>> buffer = read(scratch, gltf_path);
    if (!buffer) {
      return format(arena, "Failed to read {}: {}", gltf_path, buffer.error());
    }
    Result<JsonValue, JsonErrorInfo> json_parse_result =
        json_parse(scratch, {buffer->m_data, buffer->m_size});
    if (!json_parse_result) {
      JsonErrorInfo error = json_parse_result.error();
      return format(arena, "{}:{}:{}: {}", gltf_path, error.line, error.column,
                    error.error);
    }
    gltf = *json_parse_result;
    // TODO(mbargatin): gltf parsing.
  }

  IoResult<Span<char>> bin = read(scratch, bin_path);
  if (!bin) {
    return format(arena, "Failed to read {}: {}", bin_path, bin.error());
  }

  JsonValue gltf_mesh = json_array_value(gltf, "meshes")[meta_mesh.mesh_id];
  JsonValue gltf_primitive =
      json_array_value(gltf_mesh, "primitives")[meta_mesh.primitive_id];
  JsonValue attributes = json_value(gltf_primitive, "attributes");
  auto positions = accessor_data<glm::vec3>(
      *bin, gltf, json_integer_value(attributes, "POSITION"));
  auto normals = accessor_data<glm::vec3>(
      *bin, gltf, json_integer_value(attributes, "NORMAL"));
  Span<const glm::vec4> tangents;
  JsonValue tangent_accessor = json_value(attributes, "TANGENT");
  if (tangent_accessor) {
    tangents =
        accessor_data<glm::vec4>(*bin, gltf, json_integer(tangent_accessor));
  }
  Span<const glm::vec2> uvs;
  JsonValue uv_accessor = json_value(attributes, "TEXCOORD_0");
  if (uv_accessor) {
    uvs = accessor_data<glm::vec2>(*bin, gltf, json_integer(uv_accessor));
  }
  Span<const glm::vec4> colors;
  JsonValue color_accessor = json_value(attributes, "COLOR_0");
  if (color_accessor) {
    colors = accessor_data<glm::vec4>(*bin, gltf, json_integer(color_accessor));
  }
  auto indices = accessor_data<const u32>(
      *bin, gltf, json_integer_value(gltf_primitive, "indices"));

  Blob blob = bake_mesh_to_memory(scratch, {
                                               .num_vertices = positions.m_size,
                                               .positions = positions.m_data,
                                               .normals = normals.m_data,
                                               .tangents = tangents.m_data,
                                               .uvs = uvs.m_data,
                                               .colors = colors.m_data,
                                               .indices = indices,
                                           });

  // TODO(mbargatin): save file safely (avoid saving a partially written file by
  // first writing to a temp file, flushing to disk and then renaming).
  std::ignore = create_directories(blob_path.parent());
  IoResult<void> write_result =
      write(blob_path, Span((const char *)blob.data, blob.size));
  if (!write_result) {
    return format(arena, "Failed to write {}: {}", blob_path,
                  write_result.error());
  }

  return {};
}

void launch_asset_compilation(NotNull<EditorContext *> ctx,
                              AssetCompilationScope scope) {
  EditorProjectContext *project = ctx->m_project;
  EditorAssetCompilerSession *session = &project->m_asset_compiler.m_session;
  *session = {};

  ScratchArena scratch;
  Arena arena = Arena::from_tag(ArenaNamedTag::EditorCompile);

  auto job_data = Span<MeshCompileJobPayload>::allocate(
      &arena, project->m_meshes.raw_size());
  usize num_jobs = 0;

  for (const auto &[_, gltf] : project->m_gltf_scenes) {
    Handle<EditorMesh> cursor = gltf.first_mesh;
    Path gltf_path = project->m_directory.concat(
        &arena, {ASSET_DIR, GLTF_DIR, gltf.gltf_filename});
    while (cursor) {
      const EditorMesh &mesh = project->m_meshes[cursor];
      cursor = mesh.next;
      if (scope == AssetCompilationScope::Dirty and not mesh.is_dirty) {
        continue;
      }
      job_data[num_jobs++] = {
          .gltf_path = gltf_path,
          .blob_path = project->m_directory.concat(
              &arena, {CONTENT_DIR, MESH_DIR,
                       Path::init(to_string(scratch, mesh.guid))}),
          .guid = mesh.guid,
      };
    }
  }

  session->m_num_jobs = num_jobs;
  session->m_job_results =
      Span<MeshCompileJobResult>::allocate(&arena, num_jobs);
  job_data = job_data.subspan(0, num_jobs);

  auto job_batcher_callback = [job_data, session]() -> void {
    ScratchArena scratch;
    constexpr usize MAX_BATCH_SIZE = 64;
    for (usize job_base_index = 0; job_base_index < job_data.m_size;
         job_base_index += MAX_BATCH_SIZE) {
      if (std::atomic_ref(session->m_stop_token)
              .load(std::memory_order_relaxed)) {
        return;
      }
      usize num_batch_jobs =
          min(MAX_BATCH_SIZE, job_data.m_size - job_base_index);
      JobDesc batch_jobs[MAX_BATCH_SIZE];
      for (usize batch_job_index : range(num_batch_jobs)) {
        usize job_index = job_base_index + batch_job_index;
        const MeshCompileJobPayload *payload = &job_data[job_index];
        batch_jobs[batch_job_index] = JobDesc::init(
            scratch,
            format_zero_terminated(scratch, "Compile Mesh {}", job_index),
            [stop_token = &session->m_stop_token, payload,
             results = session->m_job_results,
             num_finished = &session->m_num_finished_jobs]() {
              if (std::atomic_ref(*stop_token)
                      .load(std::memory_order_relaxed)) {
                return;
              }
              Arena arena = Arena::from_tag(ArenaNamedTag::EditorCompile);
              Result<void, String8> compile_result =
                  compile_mesh(&arena, payload->guid, payload->gltf_path,
                               payload->blob_path);
              usize output_index = std::atomic_ref(*num_finished)
                                       .fetch_add(1, std::memory_order_relaxed);
              results[output_index] = {
                  .guid = payload->guid,
                  .error = compile_result ? "" : compile_result.error(),
              };
            });
      }
      job_dispatch_and_wait(Span(batch_jobs, num_batch_jobs));
    }
  };
  session->m_job =
      job_dispatch("Compile Batcher", std::move(job_batcher_callback));
  project->m_background_jobs.push(
      &ctx->m_project_arena, {session->m_job, ArenaNamedTag::EditorCompile});
}

} // namespace ren
