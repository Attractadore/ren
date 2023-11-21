#include "Passes.hpp"
#include "Camera.inl"
#include "ImGuiConfig.hpp"
#include "Passes/ImGui.hpp"
#include "Passes/Opaque.hpp"
#include "Passes/PostProcessing.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"

namespace ren {

namespace {

void setup_all_passes(RgBuilder &rgb, const PassesConfig &cfg) {
  assert(cfg.pipelines);
  assert(cfg.pp_opts);

  auto exposure = setup_exposure_pass(rgb, cfg.pp_opts->exposure);

  setup_opaque_passes(rgb, OpaquePassesConfig{
                               .pipelines = cfg.pipelines,
                               .exposure = exposure,
                               .viewport = cfg.viewport,
                               .early_z = cfg.early_z,
                           });

  setup_post_processing_passes(rgb, PostProcessingPassesConfig{
                                        .pipelines = cfg.pipelines,
                                        .options = cfg.pp_opts,
                                        .viewport = cfg.viewport,
                                    });
#if REN_IMGUI
  if (cfg.imgui_context) {
    setup_imgui_pass(rgb, ImGuiPassConfig{
                              .imgui_context = cfg.imgui_context,
                              .pipeline = cfg.pipelines->imgui_pass,
                              .viewport = cfg.viewport,
                          });
    rgb.present("imgui");
    return;
  }
#endif

  rgb.present("sdr");
}

struct PassesExtraData {
#if REN_IMGUI
  ImGuiContext *imgui_context = nullptr;
#endif
  bool early_z = false;
};

auto set_all_passes_data(RenderGraph &rg, const PassesData &data,
                         const PassesExtraData &extra_data) -> bool {
  assert(data.camera);
  assert(data.pp_opts);

  bool valid = true;

  valid = set_exposure_pass_data(rg, data.pp_opts->exposure);
  if (not valid) {
    return false;
  }

  const auto &camera = *data.camera;
  auto size = data.viewport_size;
  auto ar = float(size.x) / float(size.y);
  auto proj = get_projection_matrix(camera, ar);
  auto view =
      glm::lookAt(camera.position, camera.position + camera.forward, camera.up);

  valid = set_opaque_passes_data(
      rg, OpaquePassesData{
              .batch_offsets = data.batch_offsets,
              .batch_max_counts = data.batch_max_counts,
              .vertex_pool_lists = data.vertex_pool_lists,
              .meshes = data.meshes,
              .materials = data.materials,
              .mesh_instances = data.mesh_instances,
              .directional_lights = data.directional_lights,
              .viewport = data.viewport_size,
              .proj = proj,
              .view = view,
              .eye = camera.position,
              .instance_frustum_culling = data.instance_frustum_culling,
              .early_z = extra_data.early_z,
          });
  if (not valid) {
    return false;
  }

  valid = set_post_processing_passes_data(rg, *data.pp_opts);
  if (not valid) {
    return false;
  }

#if REN_IMGUI
  if (extra_data.imgui_context) {
    valid = rg.set_pass_data("imgui", RgNoPassData());
  } else {
    valid = not rg.is_pass_valid("imgui");
  }
  if (not valid) {
    return false;
  }
#endif

  return true;
}

} // namespace

void update_rg_passes(RenderGraph &rg, CommandAllocator &cmd_alloc,
                      const PassesConfig &cfg, const PassesData &data) {
  PassesExtraData extra_data = {
#if REN_IMGUI
    .imgui_context = cfg.imgui_context,
#endif
    .early_z = cfg.early_z,
  };
  bool valid = set_all_passes_data(rg, data, extra_data);
  if (!valid) {
    RgBuilder rgb(rg);
    setup_all_passes(rgb, cfg);
    rgb.build(cmd_alloc);
    valid = set_all_passes_data(rg, data, extra_data);
    ren_assert_msg(valid, "Render graph pass data update failed after rebuild");
  }
}

} // namespace ren
