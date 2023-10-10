#include "Passes.hpp"
#include "Camera.inl"
#include "ImGuiConfig.hpp"
#include "Passes/EarlyZ.hpp"
#include "Passes/ImGui.hpp"
#include "Passes/Opaque.hpp"
#include "Passes/PostProcessing.hpp"
#include "Passes/Upload.hpp"
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

  setup_upload_pass(rgb);

  auto exposure = setup_exposure_pass(rgb, cfg.pp_opts->exposure);

  if (cfg.early_z) {
    setup_early_z_pass(rgb, EarlyZPassConfig{
                                .pipeline = cfg.pipelines->early_z_pass,
                                .viewport_size = cfg.viewport_size,
                            });
  }

  setup_opaque_pass(rgb, OpaquePassConfig{
                             .pipeline = cfg.pipelines->opaque_pass,
                             .exposure = exposure,
                             .viewport_size = cfg.viewport_size,
                         });

  setup_post_processing_passes(rgb, PostProcessingPassesConfig{
                                        .pipelines = cfg.pipelines,
                                        .options = cfg.pp_opts,
                                        .size = cfg.viewport_size,
                                    });
#if REN_IMGUI
  if (ImGui::GetCurrentContext()) {
    setup_imgui_pass(rgb, ImGuiPassConfig{
                              .pipeline = cfg.pipelines->imgui_pass,
                              .fb_size = cfg.viewport_size,
                          });
    rgb.present("imgui");
    return;
  }
#endif

  rgb.present("sdr");
}

struct PassesExtraData {
  bool early_z : 1 = true;
};

auto set_all_passes_data(RenderGraph &rg, const PassesData &data,
                         const PassesExtraData &extra_data) -> bool {
  assert(data.camera);
  assert(data.pp_opts);

#define TRY_SET(...)                                                           \
  do {                                                                         \
    bool valid = __VA_ARGS__;                                                  \
    if (!valid) {                                                              \
      return false;                                                            \
    }                                                                          \
  } while (0)

  TRY_SET(rg.set_pass_data("upload",
                           UploadPassData{
                               .meshes = data.meshes,
                               .materials = data.materials,
                               .mesh_instances = data.mesh_instances,
                               .directional_lights = data.directional_lights,
                           }));

  TRY_SET(set_exposure_pass_data(rg, data.pp_opts->exposure));

  const auto &camera = *data.camera;
  auto size = data.viewport_size;
  auto ar = float(size.x) / float(size.y);
  auto proj = get_projection_matrix(camera, ar);
  auto view =
      glm::lookAt(camera.position, camera.position + camera.forward, camera.up);

  if (extra_data.early_z) {
    TRY_SET(rg.set_pass_data("early-z",
                             EarlyZPassData{
                                 .vertex_positions = data.vertex_positions,
                                 .vertex_indices = data.vertex_indices,
                                 .meshes = data.meshes,
                                 .mesh_instances = data.mesh_instances,
                                 .viewport_size = size,
                                 .proj = proj,
                                 .view = view,
                                 .eye = camera.position,
                             }));
  } else {
    if (rg.is_pass_valid("early-z")) {
      return false;
    }
  }

  TRY_SET(rg.set_pass_data(
      "opaque", OpaquePassData{
                    .vertex_positions = data.vertex_positions,
                    .vertex_normals = data.vertex_normals,
                    .vertex_tangents = data.vertex_tangents,
                    .vertex_colors = data.vertex_colors,
                    .vertex_uvs = data.vertex_uvs,
                    .vertex_indices = data.vertex_indices,
                    .meshes = data.meshes,
                    .mesh_instances = data.mesh_instances,
                    .viewport_size = size,
                    .proj = proj,
                    .view = view,
                    .eye = camera.position,
                    .num_dir_lights = u32(data.directional_lights.size()),
                }));

  TRY_SET(set_post_processing_passes_data(rg, *data.pp_opts));

#if REN_IMGUI
  if (ImGui::GetCurrentContext()) {
    TRY_SET(rg.set_pass_data("imgui", RgNoPassData()));
  } else if (rg.is_pass_valid("imgui")) {
    return false;
  }
#endif

#undef TRY_SET

  return true;
}

} // namespace

void update_rg_passes(RenderGraph &rg, CommandAllocator &cmd_alloc,
                      const PassesConfig &cfg, const PassesData &data) {
  PassesExtraData extra_data{
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
