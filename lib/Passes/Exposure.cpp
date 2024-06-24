#include "Passes/Exposure.hpp"
#include "CommandRecorder.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "glsl/LuminanceHistogram.h"

namespace ren {

namespace {

auto get_camera_exposure(const CameraParameters &camera, float ec) -> float {
  auto ev100_pow2 = camera.aperture * camera.aperture / camera.shutter_time *
                    100.0f / camera.iso;
  auto max_luminance = 1.2f * ev100_pow2 * glm::exp2(-ec);
  return 1.0f / max_luminance;
};

void setup_camera_exposure_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                                const ExposurePassConfig &cfg) {
  auto pass = rgb.create_pass({.name = "camera-exposure"});

  RgTextureToken exposure_token;
  std::tie(*cfg.exposure, exposure_token) = pass.create_texture(
      {
          .name = "exposure",
          .format = VK_FORMAT_R32_SFLOAT,
          .width = 1,
          .height = 1,
      },
      RG_TRANSFER_DST_TEXTURE);
  *cfg.temporal_layer = 0;

  pass.set_callback([=](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
    float exposure = get_camera_exposure(scene->get_camera().params,
                                         scene->get_exposure_compensation());
    ren_assert(exposure > 0.0f);
    cmd.clear_texture(rt.get_texture(exposure_token),
                      glm::vec4(exposure, 0.0f, 0.0f, 0.0f));
  });
}

void setup_automatic_exposure_pass(RgBuilder &rgb,
                                   const ExposurePassConfig &cfg) {
  *cfg.exposure = rgb.create_texture({
      .name = "automatic-exposure",
      .format = VK_FORMAT_R32_SFLOAT,
      .width = 1,
      .height = 1,
      .num_temporal_layers = 2,
      .init_usage = RG_TRANSFER_DST_TEXTURE,
      .init_cb =
          [](Handle<Texture> texture, Renderer &, CommandRecorder &cmd) {
            cmd.clear_texture(texture, glm::vec4(1.0f / glsl::MIN_LUMINANCE));
          },
  });
  *cfg.temporal_layer = 1;
}

} // namespace

void setup_exposure_pass(RgBuilder &rgb, NotNull<const Scene *> scene,
                         const ExposurePassConfig &cfg) {
  switch (scene->get_exposure_mode()) {
  case ExposureMode::Camera:
    return setup_camera_exposure_pass(rgb, scene, cfg);
  case ExposureMode::Automatic:
    return setup_automatic_exposure_pass(rgb, cfg);
  }
  std::unreachable();
}

} // namespace ren
