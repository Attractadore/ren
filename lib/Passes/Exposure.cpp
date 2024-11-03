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

void setup_camera_exposure_pass(const PassCommonConfig &ccfg,
                                const ExposurePassConfig &cfg) {
  auto pass = ccfg.rgb->create_pass({.name = "camera-exposure"});

  if (!ccfg.rcs->exposure) {
    ccfg.rcs->exposure = ccfg.rgp->create_texture({
        .name = "exposure",
        .format = TinyImageFormat_R32_SFLOAT,
        .width = 1,
        .height = 1,
    });
  }

  RgTextureToken token;
  std::tie(*cfg.exposure, token) =
      pass.write_texture("exposure", ccfg.rcs->exposure, TRANSFER_DST_TEXTURE);
  *cfg.temporal_layer = 0;

  const SceneData &scene = *ccfg.scene;
  float exposure =
      get_camera_exposure(scene.get_camera().params, scene.exposure.ec);
  ren_assert(exposure > 0.0f);

  pass.set_callback(
      [exposure, token](Renderer &, const RgRuntime &rt, CommandRecorder &cmd) {
        cmd.clear_texture(rt.get_texture(token),
                          glm::vec4(exposure, 0.0f, 0.0f, 0.0f));
      });
}

void setup_automatic_exposure_pass(const PassCommonConfig &ccfg,
                                   const ExposurePassConfig &cfg) {
  if (!ccfg.rcs->exposure) {
    ccfg.rcs->exposure = ccfg.rgp->create_texture({
        .name = "exposure",
        .format = TinyImageFormat_R32_SFLOAT,
        .width = 1,
        .height = 1,
        .ext =
            RgTextureTemporalInfo{
                .num_temporal_layers = 2,
                .usage = TRANSFER_DST_TEXTURE,
                .cb =
                    [](Handle<Texture> texture, Renderer &,
                       CommandRecorder &cmd) {
                      cmd.clear_texture(texture,
                                        glm::vec4(1.0f / glsl::MIN_LUMINANCE));
                    },
            },

    });
  }
  *cfg.exposure = ccfg.rcs->exposure;
  *cfg.temporal_layer = 1;
}

} // namespace

} // namespace ren

void ren::setup_exposure_pass(const PassCommonConfig &ccfg,
                              const ExposurePassConfig &cfg) {
  switch (ccfg.scene->exposure.mode) {
  case ExposureMode::Camera:
    return setup_camera_exposure_pass(ccfg, cfg);
  case ExposureMode::Automatic:
    return setup_automatic_exposure_pass(ccfg, cfg);
  }
  std::unreachable();
}
