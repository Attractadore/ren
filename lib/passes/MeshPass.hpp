#pragma once
#include "../Camera.hpp"
#include "../GpuScene.hpp"
#include "../RenderGraph.hpp"
#include "../core/NotNull.hpp"
#include "../passes/Pass.hpp"

#include <fmt/format.h>

namespace ren {

struct SceneData;
struct Samplers;

namespace sh {
struct Mesh;
struct MeshInstance;
struct Material;
struct DirectionalLight;
} // namespace sh

enum class CullingPhase {
  First,
  Second,
  Final,
};

struct MeshPassBaseInfo {
  StringView pass_name;

  TempSpan<const NotNull<RgTextureId *>> color_attachments;
  TempSpan<const rhi::RenderTargetOperations> color_attachment_ops;
  TempSpan<const RgDebugName> color_attachment_names;

  NotNull<RgTextureId *> depth_attachment;
  rhi::DepthTargetOperations depth_attachment_ops;
  RgDebugName depth_attachment_name;

  Camera camera;
  glm::uvec2 viewport = {};

  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;

  CullingPhase culling_phase = CullingPhase::Final;
  RgTextureId hi_z;
};

template <DrawSet S> struct MeshPassInfo;

template <> struct MeshPassInfo<DrawSet::DepthOnly> {
  MeshPassBaseInfo base;
};

using DepthOnlyMeshPassInfo = MeshPassInfo<DrawSet::DepthOnly>;

template <> struct MeshPassInfo<DrawSet::Opaque> {
  MeshPassBaseInfo base;
  RgTextureId ssao;
};

using OpaqueMeshPassInfo = MeshPassInfo<DrawSet::Opaque>;

template <DrawSet S>
void record_mesh_pass(const PassCommonConfig &ccfg,
                      const MeshPassInfo<S> &info);

} // namespace ren
