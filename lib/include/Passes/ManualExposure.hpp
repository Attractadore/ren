#pragma once
#include "Exposure.hpp"

namespace ren {

struct ManualExposurePassConfig {
  ExposureOptions::Manual options;
};

auto setup_manual_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                const ManualExposurePassConfig &cfg)
    -> ExposurePassOutput;

} // namespace ren
