#pragma once
#include "ExposureOptions.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct ExposurePassConfig {
  ExposureOptions options;
};

struct ExposurePassOutput {
  RGBufferID exposure_buffer;
};

auto setup_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                         const ExposurePassConfig &cfg) -> ExposurePassOutput;

} // namespace ren
