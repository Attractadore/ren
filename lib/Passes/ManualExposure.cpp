#include "Passes/ManualExposure.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"

namespace ren {

auto setup_manual_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("manual-exposure");

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
    cmd.clear_texture(rg.get_texture(exposure_texture),
                      glm::vec4(exposure, 0.0f, 0.0f, 0.0f));
  });

  return {.temporal_layer = 0};
};

} // namespace ren
