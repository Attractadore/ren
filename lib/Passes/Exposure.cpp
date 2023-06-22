#include "Passes/Exposure.hpp"
#include "Passes/AutomaticExposure.hpp"
#include "Passes/CameraExposure.hpp"
#include "Passes/ManualExposure.hpp"

namespace ren {

auto setup_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                         const ExposurePassConfig &cfg) -> ExposurePassOutput {
  return cfg.options.mode.visit(
      [&](const ExposureOptions::Manual &manual) {
        return setup_manual_exposure_pass(device, rgb, {.options = manual});
      },
      [&](const ExposureOptions::Camera &camera) {
        return setup_camera_exposure_pass(device, rgb, {.options = camera});
      },
      [&](const ExposureOptions::Automatic &automatic) {
        return setup_automatic_exposure_pass(
            device, rgb,
            {
                .previous_exposure_buffer = cfg.previous_exposure_buffer,
            });
      });
}

} // namespace ren
