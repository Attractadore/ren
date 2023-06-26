#include "Passes/ManualExposure.hpp"
#include "Device.hpp"
#include "glsl/exposure.hpp"

namespace ren {

auto setup_manual_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                const ManualExposurePassConfig &cfg)
    -> ExposurePassOutput {
  assert(cfg.options.exposure > 0.0f);

  auto pass = rgb.create_pass({.name = "Manual exposure"});

  auto exposure_buffer = pass.create_upload_buffer({
      .name = "Manual exposure",
      .size = sizeof(glsl::Exposure),
  });

  pass.set_host_callback(
      [=, exposure = cfg.options.exposure](Device &device, RGRuntime &rg) {
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
