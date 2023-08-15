#include "Passes/Exposure.hpp"
#include "ExposureOptions.hpp"
#include "Passes/AutomaticExposure.hpp"
#include "Passes/CameraExposure.hpp"
#include "Passes/ManualExposure.hpp"
#include "RenderGraph.hpp"

namespace ren {

auto setup_exposure_pass(RgBuilder &rgb, const ExposureOptions &opts)
    -> ExposurePassOutput {
  return opts.mode.visit(
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

auto set_exposure_pass_data(RenderGraph &rg, const ExposureOptions &opts)
    -> bool {
  return opts.mode.visit(
      [&](const ExposureOptions::Manual &options) {
        return rg.set_pass_data("manual-exposure",
                                ManualExposurePassData{.options = options});
      },
      [&](const ExposureOptions::Camera &options) {
        return rg.set_pass_data("camera-exposure",
                                CameraExposurePassData{.options = options});
      },
      [&](const ExposureOptions::Automatic &) {
        return rg.is_pass_valid("automatic-exposure");
      });
}

} // namespace ren
