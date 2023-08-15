#include "Passes/AutomaticExposure.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "glsl/Exposure.hpp"
#include "glsl/LuminanceHistogram.hpp"

namespace ren {

auto setup_automatic_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto _ = rgb.create_pass("automatic-exposure");
  rgb.create_buffer({
      .name = "automatic-exposure-init",
      .heap = BufferHeap::Device,
      .size = sizeof(glsl::Exposure),
      .num_temporal_layers = 2,
  });
  rgb.set_buffer_init_callback(
      "automatic-exposure-init", ren_rg_buffer_init_callback {
        cmd.update_buffer(
            buffer, glsl::Exposure{.exposure = 1.0f / glsl::MIN_LUMINANCE});
      });
  return {.temporal_layer = 1};
}

} // namespace ren
