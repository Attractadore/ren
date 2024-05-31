#pragma once
#include "Support/Variant.hpp"
#include "ren/ren.hpp"

#include <glm/glm.hpp>

namespace ren {

using CameraProjection =
    Variant<CameraPerspectiveProjectionDesc, CameraOrthographicProjectionDesc>;

struct Camera {
  CameraTransformDesc transform;
  CameraProjection projection;
  CameraParameterDesc params;
};

} // namespace ren
