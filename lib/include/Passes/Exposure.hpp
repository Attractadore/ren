#pragma once
#include "ExposureOptions.hpp"
#include "RenderGraph.hpp"

namespace ren {

struct ExposurePasses {
  RgPass manual;
  RgPass camera;
  bool automatic;
};

struct ExposurePassConfig {
  const ExposureOptions *options = nullptr;
};

struct ExposurePassOutput {
  ExposurePasses passes;
  RgBuffer exposure;
  u32 temporal_offset = 0;
};

auto setup_exposure_pass(RgBuilder &rgb, const ExposurePassConfig &cfg)
    -> ExposurePassOutput;

struct ExposurePassData {
  const ExposureOptions *options = nullptr;
};

auto set_exposure_pass_data(RenderGraph &rg, const ExposurePasses &passes,
                            const ExposurePassData &data) -> bool;

} // namespace ren
