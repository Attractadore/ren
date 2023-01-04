#pragma once
#include <glm/glm.hpp>

namespace ren {

struct Camera {
  glm::mat4 proj;
  glm::mat4 view;
};

} // namespace ren
