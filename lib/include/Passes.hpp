#pragma once
#include "DenseHandleMap.hpp"
#include "RenderGraph.hpp"

namespace ren {

class TextureIDAllocator;
struct Mesh;
struct MeshInst;
struct Pipelines;
struct PostProcessingOptions;
struct Camera;

namespace glsl {

struct DirLight;
struct Material;

} // namespace glsl

struct TemporalResources {
  RGBufferID exposure_buffer;
};

struct PassesConfig {
  const TemporalResources *temporal_resources = nullptr;

  const Pipelines *pipelines = nullptr;
  TextureIDAllocator *texture_allocator = nullptr;

  glm::uvec2 viewport_size;
  const Camera *camera = nullptr;

  const DenseHandleMap<Mesh> *meshes = nullptr;
  std::span<const MeshInst> mesh_insts;

  std::span<const glsl::DirLight> directional_lights;

  std::span<const glsl::Material> materials;

  const PostProcessingOptions *pp_opts;
};

auto setup_all_passes(Device &device, RGBuilder &rgb,
                      const PassesConfig &config)
    -> std::tuple<RGTextureID, TemporalResources>;

} // namespace ren
