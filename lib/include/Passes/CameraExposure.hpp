#pragma once
#include "Passes/Exposure.hpp"

namespace ren {

struct CameraExposurePassData {
  ExposureOptions::Camera options;
};

auto setup_camera_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput;

} // namespace ren
