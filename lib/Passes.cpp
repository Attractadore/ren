#include "Passes.hpp"
#include "Camera.inl"
#include "Passes/Color.hpp"
#include "Passes/Exposure.hpp"
#include "Passes/PostProcessing.hpp"
#include "Passes/Upload.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"

namespace ren {

auto setup_all_passes(Device &device, RGBuilder &rgb,
                      const PassesConfig &config)
    -> std::tuple<RGTextureID, TemporalResources> {
  assert(config.temporal_resources);
  assert(config.meshes);
  assert(config.camera);
  assert(config.pipelines);
  assert(config.texture_allocator);
  assert(config.pp_opts);

  auto temporal_resources = *config.temporal_resources;

  auto frame_resources =
      setup_upload_pass(device, rgb,
                        {
                            .mesh_insts = config.mesh_insts,
                            .directional_lights = config.directional_lights,
                            .materials = config.materials,
                        });

  auto [exposure_buffer] = setup_exposure_pass(
      device, rgb,
      {
          .previous_exposure_buffer = temporal_resources.exposure_buffer,
          .options = config.pp_opts->exposure,
      });

  // Draw scene
  auto [texture] = setup_color_pass(
      device, rgb,
      {
          .meshes = config.meshes,
          .mesh_insts = config.mesh_insts,
          .uploaded_vertex_buffers = config.uploaded_vertex_buffers,
          .uploaded_index_buffers = config.uploaded_index_buffers,
          .uploaded_textures = config.uploaded_textures,
          .transform_matrix_buffer = frame_resources.transform_matrix_buffer,
          .normal_matrix_buffer = frame_resources.normal_matrix_buffer,
          .directional_lights_buffer = frame_resources.dir_lights_buffer,
          .materials_buffer = frame_resources.materials_buffer,
          .exposure_buffer = exposure_buffer,
          .pipeline = config.pipelines->color_pass,
          .texture_allocator = config.texture_allocator,
          .size = config.viewport_size,
          .proj = get_projection_matrix(*config.camera,
                                        float(config.viewport_size.x) /
                                            float(config.viewport_size.y)),
          .view = get_view_matrix(*config.camera),
          .eye = config.camera->position,
          .num_dir_lights = u32(config.directional_lights.size()),
      });

  auto pp = setup_post_processing_passes(
      device, rgb,
      {
          .texture = texture,
          .previous_exposure_buffer = exposure_buffer,
          .pipelines = config.pipelines,
          .texture_allocator = config.texture_allocator,
          .options = config.pp_opts,
      });
  texture = pp.texture;
  temporal_resources.exposure_buffer = pp.exposure_buffer;

  return {texture, temporal_resources};
}

} // namespace ren
