#pragma once
#include "Support/Variant.hpp"
#include "ren/ren.h"

#include <glm/glm.hpp>

namespace ren {

using PerspectiveProjection = RenPerspectiveProjection;
using OrthographicProjection = RenOrthographicProjection;

using CameraProjection = Variant<PerspectiveProjection, OrthographicProjection>;

struct Camera {
  glm::vec3 position;
  glm::vec3 forward;
  glm::vec3 up;
  CameraProjection projection;
};

} // namespace ren
