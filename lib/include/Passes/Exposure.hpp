#pragma once
#include "Support/StdDef.hpp"

namespace ren {

class RenderGraph;
class RgBuilder;
struct ExposureOptions;

struct ExposurePassOutput {
  u32 temporal_layer = 0;
};

auto setup_exposure_pass(RgBuilder &rgb, const ExposureOptions &opts)
    -> ExposurePassOutput;

auto set_exposure_pass_data(RenderGraph &rg, const ExposureOptions &opts)
    -> bool;

} // namespace ren
