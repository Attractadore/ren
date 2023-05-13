#pragma once
#include "Mesh.hpp"

#include <glm/glm.hpp>

namespace ren {

struct MeshInst {
  Handle<Mesh> mesh;
  unsigned material;
  glm::mat4 matrix;
};

} // namespace ren
