#pragma once
#include "Guid.hpp"
#include "ren/core/FileSystem.hpp"
#include "ren/core/Job.hpp"
#include "ren/core/StdDef.hpp"

namespace ren {

struct EditorContext;

struct MeshCompileJobPayload {
  Path gltf_path;
  Path blob_path;
  Guid64 guid;
};

struct alignas(CACHE_LINE_SIZE) MeshCompileJobResult {
  Guid64 guid;
  String8 error;
};

struct EditorAssetCompilerSession {
  JobToken m_job;
  u32 m_num_jobs = 0;
  alignas(CACHE_LINE_SIZE) bool m_stop_token = false;
  alignas(CACHE_LINE_SIZE) u32 m_num_finished_jobs = 0;
  Span<MeshCompileJobResult> m_job_results = {};
};

struct EditorAssetCompiler {
  EditorAssetCompilerSession m_session;
};

enum class AssetCompilationScope {
  Dirty,
  All,
};

void launch_asset_compilation(
    NotNull<EditorContext *> ctx,
    AssetCompilationScope scope = AssetCompilationScope::Dirty);

} // namespace ren
