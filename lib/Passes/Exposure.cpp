#include "Passes/Exposure.hpp"
#include "CommandRecorder.hpp"
#include "ExposureOptions.hpp"
#include "RenderGraph.hpp"
#include "glsl/LuminanceHistogram.hpp"

namespace ren {

namespace {

struct ManualExposurePassData {
  ExposureOptions::Manual options;
};

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

auto get_camera_exposure(const ExposureOptions::Camera &camera) -> float {
  auto ev100_pow2 = camera.aperture * camera.aperture / camera.shutter_time *
                    100.0f / camera.iso;
  auto max_luminance =
      1.2f * ev100_pow2 * glm::exp2(-camera.exposure_compensation);
  return 1.0f / max_luminance;
};

struct CameraExposurePassData {
  ExposureOptions::Camera options;
};

auto setup_camera_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("camera-exposure");

  RgTextureId exposure_texture = pass.create_texture(
      {
          .name = "exposure",
          .format = VK_FORMAT_R32_SFLOAT,
          .width = 1,
          .height = 1,
      },
      RG_TRANSFER_DST_TEXTURE);

  pass.set_transfer_callback(ren_rg_transfer_callback(CameraExposurePassData) {
    auto exposure = get_camera_exposure(data.options);
    assert(exposure > 0.0f);
    cmd.clear_texture(rg.get_texture(exposure_texture),
                      glm::vec4(exposure, 0.0f, 0.0f, 0.0f));
  });

  return {.temporal_layer = 0};
}

auto setup_automatic_exposure_pass(RgBuilder &rgb) -> ExposurePassOutput {
  auto pass = rgb.create_pass("automatic-exposure");
  pass.set_host_callback(ren_rg_host_callback(RgNoPassData){});
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
