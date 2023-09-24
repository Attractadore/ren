#pragma once
#include "ExposureOptions.hpp"
#include "Passes/Exposure.hpp"

namespace ren {

auto setup_camera_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput;

struct CameraExposurePassData {
  ExposureOptions::Camera options;
};

} // namespace ren
