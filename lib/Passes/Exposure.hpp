#pragma once
#include "Camera.hpp"
#include "Support/StdDef.hpp"
#include "ren/ren.hpp"

namespace ren {

const char CAMERA_EXPOSURE_RUNTIME_CONFIG[] = "camera-exposure-runtime-config";
const char AUTOMATIC_EXPOSURE_RUNTIME_CONFIG[] =
    "automatic-exposure-runtime-config";

class RgBuilder;

struct ExposurePassConfig {
  ExposureMode mode;
};

struct ExposurePassOutput {
  u32 temporal_layer = 0;
};

struct CameraExposureRuntimeConfig {
  CameraParameters cam_params;
  float ec = 0.0f;
};

struct AutomaticExposureRuntimeConfig {
  float ec = 0.0f;
};

auto setup_exposure_pass(RgBuilder &rgb, const ExposurePassConfig &cfg)
    -> ExposurePassOutput;

} // namespace ren
