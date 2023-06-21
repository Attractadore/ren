#include "Passes/AutomaticExposure.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "PipelineLoading.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/exposure.hpp"
#include "glsl/luminance_histogram.hpp"

namespace ren {

auto setup_automatic_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                   const AutomaticExposurePassConfig &cfg)
    -> ExposurePassOutput {
  if (cfg.previous_exposure_buffer) {
    return {
        .exposure_buffer = cfg.previous_exposure_buffer,
    };
  }

  auto pass = rgb.create_pass({
      .name = "Automatic exposure: set initial exposure",
  });

  auto exposure_buffer = pass.create_buffer({
      .name = "Initial automatic exposure",
      .heap = BufferHeap::Upload,
      .size = sizeof(glsl::Exposure),
  });

  pass.set_callback(
      [exposure_buffer](Device &device, RGRuntime &rg, CommandBuffer &cmd) {
        auto *exposure_ptr =
            device.map_buffer<glsl::Exposure>(rg.get_buffer(exposure_buffer));
        *exposure_ptr = {
            .exposure = 1.0f / glsl::MIN_LUMINANCE,
        };
      });

  return {
      .exposure_buffer = exposure_buffer,
  };
}

} // namespace ren
