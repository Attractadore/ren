#pragma once
#include "Model.hpp"
#include "RenderGraph.hpp"
#include "glsl/Lighting.hpp"
#include "glsl/Material.hpp"

namespace ren {

struct UploadPassOutput {
  RgPass pass;
  RgBuffer transform_matrices;
  RgBuffer normal_matrices;
  RgBuffer directional_lights;
  RgBuffer materials;
};

auto setup_upload_pass(RgBuilder &rgb) -> UploadPassOutput;

struct UploadPassData {
  Span<const MeshInst> mesh_insts;
  Span<const glsl::DirLight> directional_lights;
  Span<const glsl::Material> materials;
};

} // namespace ren
