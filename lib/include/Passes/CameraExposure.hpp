#pragma once
#include "Exposure.hpp"

namespace ren {

struct CameraExposurePassConfig {
  ExposureOptions::Camera options;
};

auto setup_camera_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                const CameraExposurePassConfig &cfg)
    -> ExposurePassOutput;

} // namespace ren
