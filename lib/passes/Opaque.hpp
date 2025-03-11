#pragma once
#include "../GpuScene.hpp"
#include "Pass.hpp"

namespace ren {

struct RgGpuScene;

struct OpaquePassesConfig {
  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
  RgTextureId exposure;
  RgTextureId dhr_lut;
  NotNull<RgTextureId *> depth_buffer;
  NotNull<RgTextureId *> hdr;
};

void setup_opaque_passes(const PassCommonConfig &ccfg,
                         const OpaquePassesConfig &cfg);

} // namespace ren
