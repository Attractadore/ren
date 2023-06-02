#include "Passes/CameraExposure.hpp"
#include "Device.hpp"
#include "glsl/exposure.hpp"

namespace ren {
namespace {

auto get_camera_exposure(const ExposureOptions::Camera &camera) -> float {
  auto ev100_pow2 = camera.aperture * camera.aperture / camera.shutter_time *
                    100.0f / camera.iso;
  auto max_luminance =
      1.2f * ev100_pow2 * glm::exp2(-camera.exposure_compensation);
  return 1.0f / max_luminance;
};

} // namespace

auto setup_camera_exposure_pass(Device &device, RenderGraph::Builder &rgb,
                                const CameraExposurePassConfig &cfg)
    -> ExposurePassOutput {
  auto exposure = get_camera_exposure(cfg.options);
  assert(exposure > 0.0f);

  auto pass = rgb.create_pass({
      .name = "Camera exposure",
  });

  auto exposure_buffer = pass.create_buffer({
      .name = "Camera exposure",
      .heap = BufferHeap::Upload,
      .size = sizeof(glsl::Exposure),
  });

  pass.set_callback([exposure_buffer, exposure](Device &device, RenderGraph &rg,
                                                CommandBuffer &cmd) {
    auto *exposure_ptr =
        device.map_buffer<glsl::Exposure>(rg.get_buffer(exposure_buffer));
    *exposure_ptr = {
        .exposure = exposure,
    };
  });

  return {
      .exposure_buffer = exposure_buffer,
  };
}

} // namespace ren
