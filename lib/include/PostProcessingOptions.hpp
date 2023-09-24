#pragma once
#include "ExposureOptions.hpp"
#include "ToneMappingOptions.hpp"

namespace ren {

struct PostProcessingOptions {
  ExposureOptions exposure;
  ToneMappingOptions tone_mapping;
};

} // namespace ren
