#pragma once
#include "Passes/Exposure.hpp"

namespace ren {

auto setup_automatic_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput;

} // namespace ren
