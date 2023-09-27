#pragma once
#include "Support/StdDef.hpp"
#include "glsl/Mesh.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Mesh {
  u32 base_vertex = 0;
  u32 base_tangent_vertex = glsl::MESH_ATTRIBUTE_UNUSED;
  u32 base_color_vertex = glsl::MESH_ATTRIBUTE_UNUSED;
  u32 base_uv_vertex = glsl::MESH_ATTRIBUTE_UNUSED;
  u32 base_index = 0;
  u32 num_indices = 0;
};

struct MeshInstance {
  u32 mesh = 0;
  u32 material = 0;
  glm::mat4 matrix{1.0f};
};

} // namespace ren
