#include "Passes/AutomaticExposure.hpp"

namespace ren {

auto setup_automatic_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  return {
      .passes = {.automatic = true},
      .exposure = rgb.declare_buffer(),
      .temporal_offset = 1,
  };
}

} // namespace ren
