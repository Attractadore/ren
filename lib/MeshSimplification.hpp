#pragma once
#include "core/Vector.hpp"
#include "ren/core/NotNull.hpp"
#include "sh/Geometry.h"

#include <glm/glm.hpp>

namespace ren {

struct LOD {
  u32 base_index = 0;
  u32 num_indices = 0;
};

struct MeshSimplificationOptions {
  NotNull<Vector<glm::vec3> *> positions;
  NotNull<Vector<glm::vec3> *> normals;
  Vector<glm::vec4> *tangents = nullptr;
  Vector<glm::vec2> *uvs = nullptr;
  Vector<glm::vec4> *colors = nullptr;
  NotNull<Vector<u32> *> indices;
  NotNull<StaticVector<LOD, sh::MAX_NUM_LODS> *> lods;
  u32 num_lods = sh::MAX_NUM_LODS;
  /// Percentage of triangles to retain at each LOD
  float threshold = 0.75f;
  /// Number of LOD triangles after which to stop simplification.
  u32 min_num_triangles = 1;
};

void mesh_simplify(const MeshSimplificationOptions &opts);

} // namespace ren
