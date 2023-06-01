#pragma once
#include "DenseHandleMap.hpp"
#include "Model.hpp"
#include "RenderGraph.hpp"
#include "glsl/lighting.hpp"

#include <span>

namespace ren {

struct UploadPassConfig {
  std::span<const MeshInst> mesh_insts;
  std::span<const glsl::DirLight> directional_lights;
  std::span<const glsl::Material> materials;
};

struct UploadPassOutput {
  RGBufferID transform_matrix_buffer;
  RGBufferID normal_matrix_buffer;
  RGBufferID dir_lights_buffer;
  RGBufferID materials_buffer;
};

auto setup_upload_pass(Device &device, RenderGraph::Builder &rgb,
                       const UploadPassConfig &cfg) -> UploadPassOutput;

} // namespace ren
