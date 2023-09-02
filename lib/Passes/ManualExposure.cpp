#include "Passes/ManualExposure.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"

namespace ren {

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("manual-exposure");

  RgBufferId exposure_buffer = pass.create_buffer(
      {
          .name = "manual-exposure",
          .heap = BufferHeap::Upload,
          .size = sizeof(float),
      },
      RG_HOST_WRITE_BUFFER | RG_TRANSFER_SRC_BUFFER);

  RgTextureId exposure_texture = pass.create_texture(
      {
          .name = "exposure",
          .format = VK_FORMAT_R32_SFLOAT,
          .width = 1,
          .height = 1,
      },
      RG_TRANSFER_DST_TEXTURE);

  pass.set_transfer_callback(ren_rg_transfer_callback(ManualExposurePassData) {
    float exposure = data.options.exposure;
    assert(exposure > 0.0f);
    float *exposure_ptr = rg.map_buffer<float>(exposure_buffer);
    *exposure_ptr = exposure;
    cmd.copy_buffer_to_texture(rg.get_buffer(exposure_buffer),
                               rg.get_texture(exposure_texture));
  });

  return {.temporal_layer = 0};
};

} // namespace ren
