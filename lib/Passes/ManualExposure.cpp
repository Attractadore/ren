#include "Passes/ManualExposure.hpp"
#include "Device.hpp"
#include "glsl/Exposure.hpp"

namespace ren {

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass({
      .name = "Manual exposure",
      .type = RgPassType::Host,
  });

  auto [exposure_buffer, rt_exposure_buffer] = pass.create_buffer({
      .name = "Manual exposure",
      .size = sizeof(glsl::Exposure),
      .heap = BufferHeap::Upload,
      .usage = RG_HOST_WRITE_BUFFER,
  });

  pass.set_host_callback(ren_rg_host_callback(ManualExposurePassData) {
    float exposure = data.options.exposure;
    assert(exposure > 0.0f);
    auto *exposure_ptr = rg.map_buffer<glsl::Exposure>(rt_exposure_buffer);
    *exposure_ptr = {.exposure = exposure};
  });

  return {
      .passes = {.manual = pass},
      .exposure = exposure_buffer,
  };
};

} // namespace ren
