#pragma once
#include "ren/core/Arena.hpp"
#include "ren/core/NotNull.hpp"
#include "ren/core/Span.hpp"

#include <glm/glm.hpp>

namespace ren {

struct LOD {
  u32 base_index = 0;
  u32 num_indices = 0;
};

struct MeshSimplificationInput {
  usize num_vertices = 0;
  NotNull<const glm::vec3 *> positions;
  NotNull<const glm::vec3 *> normals;
  const glm::vec4 *tangents = nullptr;
  const glm::vec2 *uvs = nullptr;
  const glm::vec4 *colors = nullptr;
  NotNull<Span<u32> *> indices;

  NotNull<u32 *> num_lods;
  NotNull<LOD *> lods;
  /// Percentage of triangles to retain at each LOD
  float threshold = 0.75f;
  /// Number of LOD triangles after which to stop simplification.
  u32 min_num_triangles = 1;
};

void mesh_simplify(NotNull<Arena *> arena,
                   const MeshSimplificationInput &input);

} // namespace ren
