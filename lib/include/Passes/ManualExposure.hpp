#pragma once
#include "Passes/Exposure.hpp"

namespace ren {

struct ManualExposurePassData {
  ExposureOptions::Manual options;
};

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput;

} // namespace ren
