#pragma once
#include "GpuScene.hpp"
#include "Pass.hpp"
#include "core/NotNull.hpp"

namespace ren {

auto rg_import_gpu_scene(RgBuilder &rgb, const GpuScene &scene) -> RgGpuScene;
void rg_export_gpu_scene(const RgBuilder &rgb, const RgGpuScene &rg_gpu_scene,
                         NotNull<GpuScene *> gpu_scene);

struct GpuSceneUpdatePassConfig {
  NotNull<GpuScene *> gpu_scene;
  NotNull<RgGpuScene *> rg_gpu_scene;
};

void setup_gpu_scene_update_pass(const PassCommonConfig &ccfg,
                                 const GpuSceneUpdatePassConfig &cfg);

} // namespace ren
