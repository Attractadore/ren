#include "Passes/CameraExposure.hpp"
#include "RenderGraph.hpp"
#include "glsl/Exposure.hpp"

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

auto setup_camera_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("camera-exposure");

  rgb.create_buffer({
      .name = "camera-exposure-init",
      .heap = BufferHeap::Upload,
      .size = sizeof(glsl::Exposure),
  });

  auto exposure_buffer = pass.write_buffer("exposure", "camera-exposure-init",
                                           RG_HOST_WRITE_BUFFER);

  pass.set_host_callback(ren_rg_host_callback(CameraExposurePassData) {
    auto exposure = get_camera_exposure(data.options);
    assert(exposure > 0.0f);
    auto *exposure_ptr = rg.map_buffer<glsl::Exposure>(exposure_buffer);
    *exposure_ptr = {.exposure = exposure};
  });

  return {.temporal_layer = 0};
}

} // namespace ren
