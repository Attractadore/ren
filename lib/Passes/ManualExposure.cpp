#include "Passes/ManualExposure.hpp"
#include "RenderGraph.hpp"
#include "glsl/Exposure.hpp"

namespace ren {

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("manual-exposure");

  rgb.create_buffer({
      .name = "manual-exposure-init",
      .heap = BufferHeap::Upload,
      .size = sizeof(glsl::Exposure),
  });

  auto exposure_buffer = pass.write_buffer("exposure", "manual-exposure-init",
                                           RG_HOST_WRITE_BUFFER);

  pass.set_host_callback(ren_rg_host_callback(ManualExposurePassData) {
    float exposure = data.options.exposure;
    assert(exposure > 0.0f);
    auto *exposure_ptr = rg.map_buffer<glsl::Exposure>(exposure_buffer);
    *exposure_ptr = {.exposure = exposure};
  });

  return {.temporal_layer = 0};
};

} // namespace ren
