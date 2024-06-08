#include "Passes/Exposure.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "glsl/LuminanceHistogram.h"

namespace ren {

namespace {

auto get_camera_exposure(const CameraParameters &camera, float ec) -> float {
  auto ev100_pow2 = camera.aperture * camera.aperture / camera.shutter_time *
                    100.0f / camera.iso;
  auto max_luminance = 1.2f * ev100_pow2 * glm::exp2(-ec);
  return 1.0f / max_luminance;
};

auto setup_camera_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("camera-exposure");

  rgb.create_parameter<CameraExposureRuntimeConfig>(
      CAMERA_EXPOSURE_RUNTIME_CONFIG);

  RgParameterId cfg = pass.read_parameter(CAMERA_EXPOSURE_RUNTIME_CONFIG);

  RgTextureId exposure_texture = pass.create_texture(
      {
          .name = "exposure",
          .format = VK_FORMAT_R32_SFLOAT,
          .width = 1,
          .height = 1,
      },
      RG_TRANSFER_DST_TEXTURE);

  pass.set_callback([=](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
    const auto &[cam_params, ec] =
        rt.get_parameter<CameraExposureRuntimeConfig>(cfg);
    float exposure = get_camera_exposure(cam_params, ec);
    assert(exposure > 0.0f);
    cmd.clear_texture(rt.get_texture(exposure_texture),
                      glm::vec4(exposure, 0.0f, 0.0f, 0.0f));
  });

  return {.temporal_layer = 0};
}

auto setup_automatic_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  rgb.create_parameter<AutomaticExposureRuntimeConfig>(
      AUTOMATIC_EXPOSURE_RUNTIME_CONFIG);
  rgb.create_texture({
      .name = "automatic-exposure-init",
      .format = VK_FORMAT_R32_SFLOAT,
      .width = 1,
      .height = 1,
      .num_temporal_layers = 2,
      .clear = glm::vec4(1.0f / glsl::MIN_LUMINANCE),
  });
  return {.temporal_layer = 1};
}

} // namespace

auto setup_exposure_pass(RgBuilder &rgb, const ExposurePassConfig &cfg)
    -> ExposurePassOutput {
  switch (cfg.mode) {
  case ExposureMode::Camera:
    return setup_camera_exposure_pass(rgb);
  case ExposureMode::Automatic:
    return setup_automatic_exposure_pass(rgb);
  }
  std::unreachable();
}

} // namespace ren
