#include "Exposure.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../glsl/LuminanceHistogram.h"

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
  if (!ccfg.rcs->exposure) {
    ccfg.rcs->exposure = ccfg.rgp->create_texture({
        .name = "exposure",
        .format = TinyImageFormat_R32_SFLOAT,
        .width = 1,
        .height = 1,
    });
  }
  *cfg.exposure = ccfg.rcs->exposure;

  float exposure = get_camera_exposure(ccfg.scene->get_camera().params,
                                       ccfg.scene->exposure.ec);
  ren_assert(exposure > 0.0f);

  ccfg.rgb->clear_texture("exposure", cfg.exposure, {exposure, 0, 0, 0});
}

void setup_automatic_exposure_pass(const PassCommonConfig &ccfg,
                                   const ExposurePassConfig &cfg) {
  if (!ccfg.rcs->exposure) {
    ccfg.rcs->exposure = ccfg.rgp->create_texture({
        .name = "exposure",
        .format = TinyImageFormat_R32_SFLOAT,
        .width = 1,
        .height = 1,
        .persistent = true,
    });
    *cfg.exposure = ccfg.rcs->exposure;
    ccfg.rgb->clear_texture("exposure", cfg.exposure,
                            {1.0f / glsl::MIN_LUMINANCE, 0, 0, 0});
  } else {
    *cfg.exposure = ccfg.rcs->exposure;
  }
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
