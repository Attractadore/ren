#include "Passes/Exposure.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Passes/AutomaticExposure.hpp"
#include "Passes/CameraExposure.hpp"
#include "Passes/ManualExposure.hpp"
#include "PipelineLoading.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/exposure.hpp"
#include "glsl/postprocess_interface.hpp"

namespace ren {

auto setup_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                         const ExposurePassConfig &cfg) -> ExposurePassOutput {
  return cfg.options.mode.visit(
      [&](const ExposureOptions::Manual &manual) {
        return setup_manual_exposure_pass(device, rgb,
                                          ManualExposurePassConfig{
                                              .options = manual,
                                          });
      },
      [&](const ExposureOptions::Camera &camera) {
        return setup_camera_exposure_pass(device, rgb,
                                          CameraExposurePassConfig{
                                              .options = camera,
                                          });
      },
      [&](const ExposureOptions::Automatic &automatic) {
        return setup_automatic_exposure_setup_pass(
            device, rgb,
            AutomaticExposureSetupPassConfig{
                .previous_exposure_buffer = automatic.previous_exposure_buffer,
            });
      });
}

} // namespace ren
