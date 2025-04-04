#pragma once
#include "../GpuScene.hpp"
#include "../core/NotNull.hpp"
#include "Pass.hpp"

namespace ren {

auto rg_import_gpu_scene(RgBuilder &rgb, const GpuScene &scene) -> RgGpuScene;

struct GpuSceneUpdatePassConfig {
  NotNull<GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
};

void setup_gpu_scene_update_pass(const PassCommonConfig &ccfg,
                                 const GpuSceneUpdatePassConfig &cfg);

} // namespace ren
