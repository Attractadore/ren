#include "Passes/ManualExposure.hpp"
#include "Device.hpp"
#include "glsl/Exposure.hpp"

namespace ren {

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass({.name = "Manual exposure"});

  auto [exposure_buffer, rt_exposure_buffer] = pass.create_upload_buffer({
      .name = "Manual exposure",
      .size = sizeof(glsl::Exposure),
  });

  pass.set_host_callback(ren_rg_host_callback(ManualExposurePassData) {
    float exposure = data.options.exposure;
    assert(exposure > 0.0f);
    auto *exposure_ptr =
        device.map_buffer<glsl::Exposure>(rg.get_buffer(rt_exposure_buffer));
    *exposure_ptr = {.exposure = exposure};
  });

  return {
      .passes = {.manual = pass},
      .exposure = exposure_buffer,
  };
};

} // namespace ren
