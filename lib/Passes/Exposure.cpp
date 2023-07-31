#include "Passes/Exposure.hpp"
#include "Passes/AutomaticExposure.hpp"
#include "Passes/CameraExposure.hpp"
#include "Passes/ManualExposure.hpp"

namespace ren {

auto setup_exposure_pass(RgBuilder &rgb, const ExposurePassConfig &cfg)
    -> ExposurePassOutput {
  assert(cfg.options);
  return cfg.options->mode.visit(
      [&](const ExposureOptions::Manual &) {
        return setup_manual_exposure_pass(rgb);
      },
      [&](const ExposureOptions::Camera &) {
        return setup_camera_exposure_pass(rgb);
      },
      [&](const ExposureOptions::Automatic &) {
        return setup_automatic_exposure_pass(rgb);
      });
}

auto set_exposure_pass_data(RenderGraph &rg, const ExposurePasses &passes,
                            const ExposurePassData &data) -> bool {
  assert(data.options);
  return data.options->mode.visit(
      [&](const ExposureOptions::Manual &options) {
        if (passes.manual) {
          rg.set_pass_data(passes.manual,
                           ManualExposurePassData{.options = options});
          return true;
        }
        return false;
      },
      [&](const ExposureOptions::Camera &options) {
        if (passes.manual) {
          rg.set_pass_data(passes.manual,
                           CameraExposurePassData{.options = options});
          return true;
        }
        return false;
      },
      [&](const ExposureOptions::Automatic &) { return passes.automatic; });
}

} // namespace ren
