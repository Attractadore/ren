#pragma once
#include "../Camera.hpp"
#include "../GpuScene.hpp"
#include "../RenderGraph.hpp"
#include "../core/NotNull.hpp"
#include "../glsl/InstanceCullingAndLOD.h"
#include "../passes/Pass.hpp"

#include <fmt/format.h>

namespace ren {

struct SceneData;
struct Samplers;

namespace glsl {
struct Mesh;
struct MeshInstance;
struct Material;
struct DirectionalLight;
} // namespace glsl

enum class OcclusionCullingMode {
  Disabled = glsl::INSTANCE_CULLING_AND_LOD_NO_OCCLUSION_CULLING,
  FirstPhase = glsl::INSTANCE_CULLING_AND_LOD_FIRST_PHASE,
  SecondPhase = glsl::INSTANCE_CULLING_AND_LOD_SECOND_PHASE,
  ThirdPhase = glsl::INSTANCE_CULLING_AND_LOD_THIRD_PHASE,
};

struct MeshPassBaseInfo {
  StringView pass_name;

  TempSpan<const NotNull<RgTextureId *>> color_attachments;
  TempSpan<const ColorAttachmentOperations> color_attachment_ops;
  TempSpan<const RgDebugName> color_attachment_names;

  NotNull<RgTextureId *> depth_attachment;
  DepthAttachmentOperations depth_attachment_ops;
  RgDebugName depth_attachment_name;

  Camera camera;
  glm::uvec2 viewport = {};

  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;

  OcclusionCullingMode occlusion_culling_mode = OcclusionCullingMode::Disabled;
  RgTextureId hi_z;
};

template <DrawSet S> struct MeshPassInfo;

template <> struct MeshPassInfo<DrawSet::DepthOnly> {
  MeshPassBaseInfo base;
};

using DepthOnlyMeshPassInfo = MeshPassInfo<DrawSet::DepthOnly>;

template <> struct MeshPassInfo<DrawSet::Opaque> {
  MeshPassBaseInfo base;
  RgTextureId exposure;
  glm::vec3 env_luminance;
};

using OpaqueMeshPassInfo = MeshPassInfo<DrawSet::Opaque>;

template <DrawSet S>
void record_mesh_pass(const PassCommonConfig &ccfg,
                      const MeshPassInfo<S> &info);

} // namespace ren
