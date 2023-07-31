#include "Passes.hpp"
#include "Camera.inl"
#include "Passes/Exposure.hpp"
#include "Passes/Opaque.hpp"
#include "Passes/PostProcessing.hpp"
#include "Passes/Upload.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"

namespace ren {

namespace {

auto setup_all_passes(RgBuilder &rgb, const PassesConfig &cfg) -> Passes {
  assert(cfg.pipelines);
  assert(cfg.pp_opts);

  Passes passes;

  auto upload = setup_upload_pass(rgb);
  passes.upload = upload.pass;

  auto exposure = setup_exposure_pass(
      rgb, ExposurePassConfig{.options = &cfg.pp_opts->exposure});
  passes.exposure = exposure.passes;

  auto opaque = setup_opaque_pass(
      rgb, OpaquePassConfig{
               .pipeline = cfg.pipelines->opaque_pass,
               .transform_matrices = upload.transform_matrices,
               .normal_matrices = upload.normal_matrices,
               .directional_lights = upload.directional_lights,
               .materials = upload.materials,
               .exposure = exposure.exposure,
               .exposure_temporal_offset = exposure.temporal_offset,
           });
  passes.opaque = opaque.pass;

  auto pp = setup_post_processing_passes(rgb, PostProcessingPassesConfig{
                                                  .pipelines = cfg.pipelines,
                                                  .options = cfg.pp_opts,
                                                  .texture = opaque.texture,
                                                  .exposure = exposure,
                                              });
  passes.pp = pp.passes;

  rgb.present(pp.texture);

  return passes;
}

auto set_all_passes_data(RenderGraph &rg, const Passes &passes,
                         const PassesData &data) -> bool {
  assert(data.camera);
  assert(data.pp_opts);

  bool valid = true;

  ren_assert(passes.upload, "Upload pass not registered");
  rg.set_pass_data(passes.upload,
                   UploadPassData{
                       .mesh_insts = data.mesh_insts,
                       .directional_lights = data.directional_lights,
                       .materials = data.materials,
                   });

  valid = set_exposure_pass_data(
      rg, passes.exposure,
      ExposurePassData{.options = &data.pp_opts->exposure});
  if (!valid) {
    return false;
  }

  ren_assert(passes.opaque, "Opaque pass not registered");
  const auto &camera = *data.camera;
  auto size = data.viewport_size;
  auto ar = float(size.x) / float(size.y);
  auto proj = get_projection_matrix(camera, ar);
  auto view =
      glm::lookAt(camera.position, camera.position + camera.forward, camera.up);
  rg.set_pass_data(passes.opaque,
                   OpaquePassData{
                       .meshes = data.meshes,
                       .mesh_insts = data.mesh_insts,
                       .size = size,
                       .proj = proj,
                       .view = view,
                       .eye = camera.position,
                       .num_dir_lights = u32(data.directional_lights.size()),
                   });

  valid = set_post_processing_passes_data(
      rg, passes.pp, PostProcessingPassesData{.options = data.pp_opts});
  if (!valid) {
    return false;
  }

  return true;
}

} // namespace

auto update_rg_passes(RenderGraph &rg, Passes passes, const PassesConfig &cfg,
                      const PassesData &data) -> Passes {
  bool valid = set_all_passes_data(rg, passes, data);
  if (!valid) {
    RgBuilder rgb(rg);
    passes = setup_all_passes(rgb, cfg);
    rgb.build();
    valid = set_all_passes_data(rg, passes, data);
    ren_assert(valid, "Render graph pass data update failed after rebuild");
  }
  return passes;
}

} // namespace ren
