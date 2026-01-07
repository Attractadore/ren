#include "AssetCompiler.hpp"
#include "Editor.hpp"
#include "ren/baking/mesh.hpp"
#include "ren/core/Format.hpp"
#include "ren/core/GLTF.hpp"

#include <atomic>

namespace ren {

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

  Result<Gltf, GltfErrorInfo> gltf = load_gltf(scratch, {.path = gltf_path});
  if (!gltf) {
    return gltf.error().message.copy(arena);
  }

  IoResult<Span<std::byte>> bin = read<std::byte>(scratch, bin_path);
  if (!bin) {
    return format(arena, "Failed to read {}: {}", bin_path, bin.error());
  }

  if (gltf->meshes.size() <= meta_mesh.mesh_id) {
    return format(arena, "Failed to find mesh {} in {}", meta_mesh.mesh_id,
                  gltf_path);
  }
  GltfMesh gltf_mesh = gltf->meshes[meta_mesh.mesh_id];

  if (gltf_mesh.primitives.size() <= meta_mesh.primitive_id) {
    return format(arena, "Failed to find primitive {} for mesh {} in {}",
                  meta_mesh.mesh_id, meta_mesh.primitive_id, gltf_path);
  }
  GltfPrimitive gltf_primitive = gltf_mesh.primitives[meta_mesh.primitive_id];

  Blob blob = bake_mesh_to_memory(
      scratch, gltf_primitive_to_mesh_info(*bin, *gltf, gltf_primitive));

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
