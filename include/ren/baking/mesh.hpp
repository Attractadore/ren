#pragma once
#include "../ren.hpp"

namespace ren {

struct MeshInfo {
  size_t num_vertices = 0;
  const glm::vec3 *positions = nullptr;
  const glm::vec3 *normals = nullptr;
  /// Optional
  const glm::vec4 *tangents = nullptr;
  /// Optional
  const glm::vec2 *uvs = nullptr;
  /// Optional
  const glm::vec4 *colors = nullptr;
  size_t num_indices = 0;
  /// Optional
  const uint32_t *indices = nullptr;
};

[[nodiscard]] auto bake_mesh_to_file(const MeshInfo &info, FILE *out)
    -> expected<void>;

[[nodiscard]] auto bake_mesh_to_memory(const MeshInfo &info)
    -> expected<std::tuple<void *, size_t>>;

} // namespace ren
