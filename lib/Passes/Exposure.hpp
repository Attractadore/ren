#pragma once
#include "ExposureOptions.hpp"
#include "Support/StdDef.hpp"
#include "ren/ren.hpp"

namespace ren {

const char MANUAL_EXPOSURE_RUNTIME_CONFIG[] = "manual-exposure-runtime-config";
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

struct ManualExposureRuntimeConfig {
  ExposureOptions::Manual options;
};

struct CameraExposureRuntimeConfig {
  ExposureOptions::Camera options;
};

struct AutomaticExposureRuntimeConfig {
  ExposureOptions::Automatic options;
};

auto setup_exposure_pass(RgBuilder &rgb, const ExposurePassConfig &cfg)
    -> ExposurePassOutput;

} // namespace ren
