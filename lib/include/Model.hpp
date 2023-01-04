#pragma once
#include "Def.hpp"

#include <glm/glm.hpp>

namespace ren {

struct Model {
  MeshID mesh;
  MaterialID material;
  glm::mat4 matrix;
};

} // namespace ren
