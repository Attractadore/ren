#pragma once
#include "Exposure.hpp"

namespace ren {

class TextureIDAllocator;
struct Pipelines;

struct AutomaticExposureSetupPassConfig {
  Optional<RGBufferExportInfo> previous_exposure_buffer;
};

auto setup_automatic_exposure_setup_pass(
    Device &device, RenderGraph::Builder &rgb,
    const AutomaticExposureSetupPassConfig &cfg) -> ExposurePassOutput;

struct AutomaticExposureCalculationPassConfig {
  RGTextureID texture;
  RGBufferID previous_exposure_buffer;
  TextureIDAllocator *texture_allocator = nullptr;
  const Pipelines *pipelines = nullptr;
  float exposure_compensation = 0.0f;
};

struct AutomaticExposureCalculationPassOutput {
  RGBufferID exposure_buffer;
};

auto setup_automatic_exposure_calculation_pass(
    Device &device, RenderGraph::Builder &rgb,
    const AutomaticExposureCalculationPassConfig &cfg)
    -> AutomaticExposureCalculationPassOutput;

} // namespace ren
