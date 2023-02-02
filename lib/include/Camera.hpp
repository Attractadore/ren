#pragma once
#include "ren/ren.h"

#include <glm/glm.hpp>

#include <variant>

namespace ren {

using PerspectiveProjection = RenPerspectiveProjection;
using OrthographicProjection = RenOrthographicProjection;

using CameraProjection =
    std::variant<PerspectiveProjection, OrthographicProjection>;

struct Camera {
  glm::vec3 position;
  glm::vec3 forward;
  glm::vec3 up;
  CameraProjection projection;
};

} // namespace ren
