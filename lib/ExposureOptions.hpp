#pragma once
#include "Camera.hpp"
#include "ren/ren.hpp"

namespace ren {

struct ExposureOptions {
  ExposureMode mode = ExposureMode::Automatic;
  CameraParameters cam_params;
  float ec = 0.0f;
};

} // namespace ren
