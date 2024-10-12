#pragma once
#include "GpuScene.hpp"
#include "Pass.hpp"

namespace ren {

struct RgGpuScene;

struct OpaquePassesConfig {
  NotNull<RgGpuScene *> gpu_scene;
  RgTextureId exposure;
  u32 exposure_temporal_layer = 0;
  NotNull<RgTextureId *> depth_buffer;
  NotNull<RgTextureId *> hdr;
};

void setup_opaque_passes(const PassCommonConfig &ccfg,
                         const OpaquePassesConfig &cfg);

} // namespace ren
