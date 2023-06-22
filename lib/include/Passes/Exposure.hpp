#pragma once
#include "ExposureOptions.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct ExposurePassConfig {
  RGBufferID previous_exposure_buffer;
  ExposureOptions options;
};

struct ExposurePassOutput {
  RGBufferID exposure_buffer;
};

auto setup_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                         const ExposurePassConfig &cfg) -> ExposurePassOutput;

} // namespace ren
