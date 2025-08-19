#pragma once
#include "../GpuScene.hpp"
#include "MeshPass.hpp"
#include "Pass.hpp"

namespace ren {

struct RgGpuScene;

struct EarlyZPassConfig {
  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
  CullingPhase culling_phase = CullingPhase::Final;
  NotNull<RgTextureId *> depth_buffer;
  RgTextureId hi_z;
};

void setup_early_z_pass(const PassCommonConfig &ccfg,
                        const EarlyZPassConfig &cfg);

struct OpaquePassConfig {
  NotNull<const GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
  CullingPhase culling_phase = CullingPhase::Final;
  NotNull<RgTextureId *> hdr;
  NotNull<RgTextureId *> depth_buffer;
  RgTextureId hi_z;
  RgTextureId ssao;
};

void setup_opaque_pass(const PassCommonConfig &ccfg,
                       const OpaquePassConfig &cfg);

} // namespace ren
