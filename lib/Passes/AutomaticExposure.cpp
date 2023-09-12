#include "Passes/AutomaticExposure.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "glsl/LuminanceHistogram.hpp"

namespace ren {

auto setup_automatic_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("automatic-exposure");
  pass.set_host_callback(ren_rg_host_callback(RgNoPassData){});
  rgb.create_texture({
      .name = "automatic-exposure-init",
      .format = VK_FORMAT_R32_SFLOAT,
      .width = 1,
      .height = 1,
      .num_temporal_layers = 2,
      .clear = glm::vec4(1.0f / glsl::MIN_LUMINANCE),
  });
  return {.temporal_layer = 1};
}

} // namespace ren
