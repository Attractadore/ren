#pragma once
#include "DenseHandleMap.hpp"
#include "Passes/Exposure.hpp"
#include "Passes/PostProcessing.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct Mesh;
struct MeshInst;
struct Pipelines;
struct PostProcessingOptions;
struct Camera;

namespace glsl {

struct DirLight;
struct Material;

} // namespace glsl

struct Passes {
  RgPass upload;
  ExposurePasses exposure;
  RgPass opaque;
  PostProcessingPasses pp;
};

struct PassesConfig {
  const Pipelines *pipelines = nullptr;
  const PostProcessingOptions *pp_opts = nullptr;
};

struct PassesData {
  Swapchain *swapchain = nullptr;

  glm::uvec2 viewport_size;
  const Camera *camera = nullptr;

  const DenseHandleMap<Mesh> *meshes = nullptr;
  Span<const MeshInst> mesh_insts;

  Span<const glsl::DirLight> directional_lights;

  Span<const glsl::Material> materials;

  const PostProcessingOptions *pp_opts;
};

auto update_rg_passes(RenderGraph &rg, Passes passes, const PassesConfig &cfg,
                      const PassesData &data) -> Passes;

} // namespace ren
