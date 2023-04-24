#pragma once
#include "ren/ren.h"

#include <glm/glm.hpp>

namespace ren {

struct MeshInst {
  RenMesh mesh;
  RenMaterial material;
  glm::mat4 matrix;
};

} // namespace ren
