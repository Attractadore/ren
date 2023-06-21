#pragma once
#include "Exposure.hpp"

namespace ren {

struct AutomaticExposurePassConfig {
  RGBufferID previous_exposure_buffer;
};

auto setup_automatic_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                   const AutomaticExposurePassConfig &cfg)
    -> ExposurePassOutput;

} // namespace ren
