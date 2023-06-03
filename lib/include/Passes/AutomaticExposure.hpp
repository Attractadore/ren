#pragma once
#include "Exposure.hpp"

namespace ren {

struct AutomaticExposurePassConfig {
  Optional<RGBufferExportInfo> previous_exposure_buffer;
};

auto setup_automatic_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                   const AutomaticExposurePassConfig &cfg)
    -> ExposurePassOutput;

} // namespace ren
