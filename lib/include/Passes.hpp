#pragma once
#include "DenseHandleMap.hpp"
#include "Support/Span.hpp"

namespace ren {

class RenderGraph;
struct Camera;
struct Mesh;
struct MeshInst;
struct Pipelines;
struct PostProcessingOptions;

namespace glsl {

struct DirLight;
struct Material;

} // namespace glsl

struct PassesConfig {
  const Pipelines *pipelines = nullptr;
  const PostProcessingOptions *pp_opts = nullptr;
};

struct PassesData {
  glm::uvec2 viewport_size;
  const Camera *camera = nullptr;

  const DenseHandleMap<Mesh> *meshes = nullptr;
  Span<const MeshInst> mesh_insts;

  Span<const glsl::DirLight> directional_lights;

  Span<const glsl::Material> materials;

  const PostProcessingOptions *pp_opts;
};

void update_rg_passes(RenderGraph &rg, const PassesConfig &cfg,
                      const PassesData &data);

} // namespace ren
