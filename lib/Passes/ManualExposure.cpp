#include "Passes/ManualExposure.hpp"
#include "Device.hpp"
#include "glsl/exposure.hpp"

namespace ren {

auto setup_manual_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                const ManualExposurePassConfig &cfg)
    -> ExposurePassOutput {
  assert(cfg.options.exposure > 0.0f);

  auto pass = rgb.create_pass({.name = "Manual exposure"});

  auto exposure_buffer = pass.create_buffer({
      .name = "Manual exposure",
      .heap = BufferHeap::Upload,
      .size = sizeof(glsl::Exposure),
  });

  pass.set_callback([exposure_buffer, exposure = cfg.options.exposure](
                        Device &device, RenderGraph &rg, CommandBuffer &cmd) {
    auto *exposure_ptr =
        device.map_buffer<glsl::Exposure>(rg.get_buffer(exposure_buffer));
    *exposure_ptr = {
        .exposure = exposure,
    };
  });

  return {
      .exposure_buffer = exposure_buffer,
  };
};

} // namespace ren
