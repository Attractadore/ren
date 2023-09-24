#pragma once
#include "ExposureOptions.hpp"
#include "Passes/Exposure.hpp"

namespace ren {

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput;

struct ManualExposurePassData {
  ExposureOptions::Manual options;
};

} // namespace ren
