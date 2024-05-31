#pragma once
#include "ren/ren.hpp"

namespace ren {

struct ExposureOptions {
  CameraParameterDesc cam_params;
  float ec = 0.0f;
};

} // namespace ren
