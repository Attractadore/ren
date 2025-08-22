#include "MeshPass.hpp"
#include "../CommandRecorder.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../core/Views.hpp"
#include "../sh/MeshletCulling.h"
#include "../sh/StreamScan.h"
#include "EarlyZ.vert.hpp"
#include "ExclusiveScanUint32.comp.hpp"
#include "InstanceCullingAndLOD.comp.hpp"
#include "MeshletCulling.comp.hpp"
#include "MeshletSorting.comp.hpp"
#include "Opaque.frag.hpp"
#include "PrepareBatch.comp.hpp"

#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace ren {

namespace {

struct CullingInfo {
  u32 draw_set = -1;
  NotNull<RgBufferId<sh::MeshletDrawCommand> *> batch_commands;
  NotNull<RgBufferId<u32> *> batch_offsets;
  NotNull<RgBufferId<u32> *> batch_sizes;
  NotNull<RgBufferId<sh::DispatchIndirectCommand> *> batch_prepare_commands;
};

void record_culling(const PassCommonConfig &ccfg, const MeshPassBaseInfo &info,
                    RgBuilder &rgb, const CullingInfo &cfg) {
  ZoneScoped;

  const DrawSetData &ds = info.gpu_scene->draw_sets[cfg.draw_set];
  const RgDrawSetData &rg_ds = info.rg_gpu_scene->draw_sets[cfg.draw_set];

  u32 num_batches = ds.batches.size();

  u32 num_instances = ds.size();

  u32 num_meshlets = 0;
  for (auto i : range(ds.batches.size())) {
    num_meshlets += ds.batches[i].num_meshlets;
  }

  u32 buckets_size = 0;
  std::array<u32, sh::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
  for (u32 bucket : range(sh::NUM_MESHLET_CULLING_BUCKETS)) {
    bucket_offsets[bucket] = buckets_size;
    u32 bucket_stride = 1 << bucket;
    u32 bucket_size = std::min(num_instances, num_meshlets / bucket_stride);
    buckets_size += bucket_size;
  }

  auto meshlet_bucket_commands =
      rgb.create_buffer<sh::DispatchIndirectCommand>({
          .count = sh::NUM_MESHLET_CULLING_BUCKETS,
          .init = sh::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1},
      });

  auto meshlet_bucket_sizes = rgb.create_buffer<u32>({
      .count = sh::NUM_MESHLET_CULLING_BUCKETS,
      .init = 0,
  });

  auto meshlet_cull_data =
      rgb.create_buffer<sh::MeshletCullData>({.count = buckets_size});

  *cfg.batch_sizes = rgb.create_buffer<u32>({.count = num_batches, .init = 0});

  *cfg.batch_prepare_commands = rgb.create_buffer<sh::DispatchIndirectCommand>({
      .count = num_batches,
      .init = sh::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1},
  });

  auto num_commands = rgb.create_buffer<u32>({.init = 0});

  auto sort_command = rgb.create_buffer<sh::DispatchIndirectCommand>({
      .init = sh::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1},
  });

  {
    auto pass = rgb.create_pass({"instance-culling-and-lod"});

    const SceneGraphicsSettings &settings = ccfg.scene->settings;

    u32 feature_mask = 0;
    if (settings.lod_selection) {
      feature_mask |= sh::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
    }
    if (settings.instance_frustum_culling) {
      feature_mask |= sh::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
    }
    if (settings.instance_occulusion_culling) {
      feature_mask |= sh::INSTANCE_CULLING_AND_LOD_OCCLUSION_BIT;
    }

    if (info.culling_phase == CullingPhase::First) {
      feature_mask |= sh::INSTANCE_CULLING_AND_LOD_FIRST_PHASE_BIT;
    } else if (info.culling_phase == CullingPhase::Second) {
      feature_mask |= sh::INSTANCE_CULLING_AND_LOD_SECOND_PHASE_BIT;
    }

    float num_viewport_triangles =
        info.viewport.x * info.viewport.y / settings.lod_triangle_pixels;
    float lod_triangle_density = num_viewport_triangles / 4.0f;

    auto meshlet_bucket_offsets =
        ccfg.allocator->allocate<u32>(bucket_offsets.size());
    std::ranges::copy(bucket_offsets, meshlet_bucket_offsets.host_ptr);

    RgInstanceCullingAndLODArgs args = {
        .meshes = pass.read_buffer(info.rg_gpu_scene->meshes),
        .transform_matrices =
            pass.read_buffer(info.rg_gpu_scene->transform_matrices),
        .cull_data = pass.read_buffer(rg_ds.cull_data),
        .meshlet_bucket_commands = pass.write_buffer("meshlet-bucket-commands",
                                                     &meshlet_bucket_commands),
        .meshlet_bucket_offsets = meshlet_bucket_offsets.device_ptr,
        .meshlet_bucket_sizes =
            pass.write_buffer("meshlet-bucket-sizes", &meshlet_bucket_sizes),
        .meshlet_cull_data =
            pass.write_buffer("meshlet-cull-data", &meshlet_cull_data),
        .feature_mask = feature_mask,
        .num_instances = num_instances,
        .proj_view = get_projection_view_matrix(info.camera, info.viewport),
        .lod_triangle_density = lod_triangle_density,
        .lod_bias = settings.lod_bias,
    };

    if (info.culling_phase == CullingPhase::Second) {
      args.mesh_instance_visibility =
          pass.write_buffer("new-mesh-instance-visibility",
                            &info.rg_gpu_scene->mesh_instance_visibility);
      if (settings.instance_occulusion_culling) {
        ren_assert(info.hi_z);
        args.hi_z = pass.read_texture(
            info.hi_z,
            {
                .mag_filter = rhi::Filter::Nearest,
                .min_filter = rhi::Filter::Nearest,
                .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
            });
      }
    } else {
      args.mesh_instance_visibility =
          pass.read_buffer(info.rg_gpu_scene->mesh_instance_visibility);
    }

    pass.dispatch_grid(ccfg.pipelines->instance_culling_and_lod, args,
                       num_instances);
  }

  auto unsorted_batch_commands = rgb.create_buffer<sh::MeshletDrawCommand>({
      .count = sh::MAX_DRAW_MESHLETS,
  });

  auto unsorted_batch_command_batch_ids =
      rgb.create_buffer<sh::BatchId>({.count = sh::MAX_DRAW_MESHLETS});

  {
    auto pass = rgb.create_pass({"meshlet-culling"});

    struct {
      Handle<ComputePipeline> pipeline;
      RgBufferToken<sh::DispatchIndirectCommand> meshlet_bucket_commands;
      std::array<u32, sh::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
    } rcs;

    rcs.pipeline = ccfg.pipelines->meshlet_culling;
    rcs.meshlet_bucket_commands =
        pass.read_buffer(meshlet_bucket_commands, rhi::INDIRECT_COMMAND_BUFFER);
    rcs.bucket_offsets = bucket_offsets;

    RgMeshletCullingArgs args = {
        .meshes = pass.read_buffer(info.rg_gpu_scene->meshes),
        .transform_matrices =
            pass.read_buffer(info.rg_gpu_scene->transform_matrices),
        .bucket_cull_data = pass.read_buffer(meshlet_cull_data),
        .bucket_size = pass.read_buffer(meshlet_bucket_sizes),
        .batch_sizes = pass.write_buffer("batch-sizes", cfg.batch_sizes.get()),
        .batch_prepare_commands = pass.write_buffer(
            "batch-prepare-commands", cfg.batch_prepare_commands.get()),
        .commands = pass.write_buffer("unsorted-batch-commands",
                                      &unsorted_batch_commands),
        .command_batch_ids =
            pass.write_buffer("unsorted-batch-command-batch-ids",
                              &unsorted_batch_command_batch_ids),
        .num_commands =
            pass.write_buffer("unsorted-batch-command-count", &num_commands),
        .sort_command = pass.write_buffer("sort-command", &sort_command),
        .proj_view = get_projection_view_matrix(info.camera, info.viewport),
        .eye = info.camera.position,
    };

    const SceneGraphicsSettings &settings = ccfg.scene->settings;

    if (settings.meshlet_cone_culling) {
      args.feature_mask |= sh::MESHLET_CULLING_CONE_BIT;
    }
    if (settings.meshlet_frustum_culling) {
      args.feature_mask |= sh::MESHLET_CULLING_FRUSTUM_BIT;
    }
    if (settings.meshlet_occlusion_culling) {
      args.feature_mask |= sh::MESHLET_CULLING_OCCLUSION_BIT;
      if (info.culling_phase != CullingPhase::First) {
        ren_assert(info.hi_z);
        args.hi_z = pass.read_texture(
            info.hi_z,
            {
                .mag_filter = rhi::Filter::Nearest,
                .min_filter = rhi::Filter::Nearest,
                .mipmap_mode = rhi::SamplerMipmapMode::Nearest,
                .address_mode_u = rhi::SamplerAddressMode::ClampToEdge,
                .address_mode_v = rhi::SamplerAddressMode::ClampToEdge,
            });
      }
    }

    pass.set_callback(
        [rcs, args](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
          cmd.bind_compute_pipeline(rcs.pipeline);
          auto pc = to_push_constants(rg, args);
          DevicePtr<sh::MeshletCullData> base_cull_data = pc.bucket_cull_data;
          DevicePtr<u32> base_bucket_size = pc.bucket_size;
          for (u32 bucket : range(sh::NUM_MESHLET_CULLING_BUCKETS)) {
            pc.bucket_cull_data = base_cull_data + rcs.bucket_offsets[bucket];
            pc.bucket_size = base_bucket_size + bucket;
            pc.bucket = bucket;
            cmd.push_constants(pc);
            cmd.dispatch_indirect(
                rg.get_buffer(rcs.meshlet_bucket_commands).slice(bucket));
          }
        });
  }

  *cfg.batch_offsets = rgb.create_buffer<u32>({.count = num_batches});

  {
    auto block_sums = rgb.create_buffer<u32>(
        {.count = sh::get_stream_scan_block_sum_count(num_batches)});

    auto scan_num_started = rgb.create_buffer<u32>({.init = 0});

    auto scan_num_finished = rgb.create_buffer<u32>({.init = 0});

    auto pass = rgb.create_pass({"batch-sizes-scan"});

    RgStreamScanArgs args = {
        .src = pass.read_buffer(*cfg.batch_sizes),
        .block_sums = pass.write_buffer("scan-block-sums", &block_sums),
        .dst = pass.write_buffer("batch-offsets", cfg.batch_offsets.get()),
        .num_started = pass.write_buffer("scan-num-started", &scan_num_started),
        .num_finished =
            pass.write_buffer("scan-num-finished", &scan_num_finished),
        .count = num_batches,
    };

    pass.dispatch_grid(ccfg.pipelines->exclusive_scan_uint32, args,
                       num_batches);
  }

  *cfg.batch_commands = rgb.create_buffer<sh::MeshletDrawCommand>(
      {.count = sh::MAX_DRAW_MESHLETS});

  {
    RgBufferId<u32> batch_out_offsets =
        rgb.create_buffer<u32>({.count = num_batches});

    rgb.copy_buffer(*cfg.batch_offsets, &batch_out_offsets);

    auto pass = rgb.create_pass({"meshlet-sorting"});

    RgMeshletSortingArgs args = {
        .num_commands = pass.read_buffer(num_commands),
        .batch_out_offsets =
            pass.write_buffer("batch-out-offsets", &batch_out_offsets),
        .unsorted_commands = pass.read_buffer(unsorted_batch_commands),
        .unsorted_command_batch_ids =
            pass.read_buffer(unsorted_batch_command_batch_ids),
        .commands =
            pass.write_buffer("batch-commands", cfg.batch_commands.get()),
    };

    pass.dispatch_indirect(ccfg.pipelines->meshlet_sorting, args, sort_command);
  }
} // namespace

auto get_render_pass_args(const PassCommonConfig &cfg,
                          const DepthOnlyMeshPassInfo &info,
                          RgPassBuilder &pass) {
  const RgGpuScene &gpu_scene = *info.base.rg_gpu_scene;
  return RgEarlyZArgs{
      .meshes = pass.read_buffer(gpu_scene.meshes, rhi::VS_RESOURCE_BUFFER),
      .mesh_instances =
          pass.read_buffer(gpu_scene.mesh_instances, rhi::VS_RESOURCE_BUFFER),
      .transform_matrices = pass.read_buffer(gpu_scene.transform_matrices,
                                             rhi::VS_RESOURCE_BUFFER),
      .proj_view =
          get_projection_view_matrix(info.base.camera, info.base.viewport),
  };
}

auto get_render_pass_args(const PassCommonConfig &cfg,
                          const OpaqueMeshPassInfo &info, RgPassBuilder &pass) {
  const SceneData &scene = *cfg.scene;
  const RgGpuScene &gpu_scene = *info.base.rg_gpu_scene;

  return RgOpaqueArgs{
      .exposure = pass.read_buffer(gpu_scene.exposure, rhi::FS_RESOURCE_BUFFER),
      .meshes = pass.read_buffer(gpu_scene.meshes, rhi::VS_RESOURCE_BUFFER),
      .mesh_instances =
          pass.read_buffer(gpu_scene.mesh_instances, rhi::VS_RESOURCE_BUFFER),
      .transform_matrices = pass.read_buffer(gpu_scene.transform_matrices,
                                             rhi::VS_RESOURCE_BUFFER),
      .materials =
          pass.read_buffer(gpu_scene.materials, rhi::FS_RESOURCE_BUFFER),
      .directional_lights = pass.read_buffer(gpu_scene.directional_lights,
                                             rhi::FS_RESOURCE_BUFFER),
      .num_directional_lights = u32(cfg.scene->directional_lights.size()),
      .proj_view =
          get_projection_view_matrix(info.base.camera, info.base.viewport),
      .znear = info.base.camera.near,
      .eye = info.base.camera.position,
      .inv_viewport = 1.0f / glm::vec2(cfg.viewport),
      .ssao = pass.try_read_texture(
          info.ssao, rhi::FS_RESOURCE_IMAGE,
          scene.settings.ssao_full_res ? rhi::SAMPLER_NEAREST_CLAMP
                                       : rhi::SAMPLER_LINEAR_MIP_NEAREST_CLAMP),
      .env_luminance = scene.env_luminance,
      .env_map = scene.env_map,
  };
}

struct MeshRenderPassInfo {
  RgBufferId<sh::MeshletDrawCommand> batch_commands;
  RgBufferId<u32> batch_offsets;
  RgBufferId<u32> batch_sizes;
  RgBufferId<sh::DispatchIndirectCommand> batch_prepare_commands;
};

template <DrawSet S>
void record_render_pass(const PassCommonConfig &ccfg,
                        const MeshPassInfo<S> &info,
                        const MeshRenderPassInfo &cfg) {
  ZoneScoped;

  u32 draw_set = get_draw_set_index(S);

  const DrawSetData &ds = info.base.gpu_scene->draw_sets[draw_set];
  const RgDrawSetData &rg_ds = info.base.rg_gpu_scene->draw_sets[draw_set];

  const char *pass_type = "";
  if (info.base.culling_phase == CullingPhase::First) {
    pass_type = "-first-phase";
  } else if (info.base.culling_phase == CullingPhase::Second) {
    pass_type = "-second-phase";
  }

  RgBufferId<sh::DrawIndexedIndirectCommand> commands =
      ccfg.rgb->create_buffer<sh::DrawIndexedIndirectCommand>(
          {.count = sh::MAX_DRAW_MESHLETS});

  for (sh::BatchId batch : range(ds.batches.size())) {
    {
      auto pass = ccfg.rgb->create_pass({fmt::format(
          "{}{}-prepare-batch-{}", info.base.pass_name, pass_type, batch)});

      RgPrepareBatchArgs args = {
          .batch_offset = pass.read_buffer(cfg.batch_offsets, batch),
          .batch_size = pass.read_buffer(cfg.batch_sizes, batch),
          .command_descs = pass.read_buffer(cfg.batch_commands),
          .commands = pass.write_buffer(fmt::format("{}{}-batch-{}-commands",
                                                    info.base.pass_name,
                                                    pass_type, batch),
                                        &commands),
      };

      pass.dispatch_indirect(ccfg.pipelines->prepare_batch, args,
                             cfg.batch_prepare_commands, batch);
    }

    auto pass = ccfg.rgb->create_pass(
        {fmt::format("{}{}-batch-{}", info.base.pass_name, pass_type, batch)});

    for (usize i = 0; i < info.base.color_attachments.size(); ++i) {
      NotNull<RgTextureId *> color_attachment = info.base.color_attachments[i];
      if (!*color_attachment) {
        continue;
      }
      rhi::RenderTargetOperations ops = info.base.color_attachment_ops[i];
      if (info.base.culling_phase != CullingPhase::First or batch > 0) {
        ops.load = rhi::RenderPassLoadOp::Load;
      }
      std::tie(*color_attachment, std::ignore) = pass.write_render_target(
          info.base.color_attachment_names[i], *color_attachment, ops);
    }

    if (*info.base.depth_attachment) {
      if (info.base.depth_attachment_ops.store ==
          rhi::RenderPassStoreOp::None) {
        pass.read_depth_stencil_target(*info.base.depth_attachment);
      } else {
        rhi::DepthTargetOperations ops = info.base.depth_attachment_ops;
        if (info.base.culling_phase != CullingPhase::First or batch > 0) {
          ops.load = rhi::RenderPassLoadOp::Load;
        }
        std::tie(*info.base.depth_attachment, std::ignore) =
            pass.write_depth_stencil_target(info.base.depth_attachment_name,
                                            *info.base.depth_attachment, ops);
      }
    }

    struct {
      Handle<GraphicsPipeline> pipeline;
      BufferSlice<u8> indices;
      RgBufferToken<sh::DrawIndexedIndirectCommand> commands;
      RgBufferToken<u32> batch_sizes;
    } rcs;

    const BatchDesc &batch_desc =
        info.base.gpu_scene->draw_sets[draw_set].batches[batch].desc;
    rcs.pipeline = get_batch_pipeline(S, batch_desc, *ccfg.pipelines);
    rcs.indices = get_batch_indices(batch_desc, *ccfg.scene);
    rcs.commands = pass.read_buffer(commands, rhi::INDIRECT_COMMAND_BUFFER);
    rcs.batch_sizes =
        pass.read_buffer(cfg.batch_sizes, rhi::INDIRECT_COMMAND_BUFFER, batch);

    auto args = get_render_pass_args(ccfg, info, pass);

    pass.set_render_pass_callback([rcs, args](Renderer &, const RgRuntime &rg,
                                              RenderPass &render_pass) {
      render_pass.bind_graphics_pipeline(rcs.pipeline);
      render_pass.bind_index_buffer(rcs.indices);
      rg.push_constants(render_pass, args);
      render_pass.draw_indexed_indirect_count(rg.get_buffer(rcs.commands),
                                              rg.get_buffer(rcs.batch_sizes));
    });
  }
}

} // namespace

template <DrawSet S>
void record_mesh_pass(const PassCommonConfig &ccfg,
                      const MeshPassInfo<S> &info) {
  ZoneScoped;
#if REN_RG_DEBUG
  StringView pass_name = info.base.pass_name;
  ZoneText(pass_name.data(), pass_name.size());
#endif

  RgBufferId<sh::MeshletDrawCommand> batch_commands;
  RgBufferId<u32> batch_offsets, batch_sizes;
  RgBufferId<sh::DispatchIndirectCommand> batch_prepare_commands;
  record_culling(ccfg, info.base, *ccfg.rgb,
                 CullingInfo{
                     .draw_set = get_draw_set_index(S),
                     .batch_commands = &batch_commands,
                     .batch_offsets = &batch_offsets,
                     .batch_sizes = &batch_sizes,
                     .batch_prepare_commands = &batch_prepare_commands,
                 });

  record_render_pass(ccfg, info,
                     MeshRenderPassInfo{
                         .batch_commands = batch_commands,
                         .batch_offsets = batch_offsets,
                         .batch_sizes = batch_sizes,
                         .batch_prepare_commands = batch_prepare_commands,
                     });
}

#define add_mesh_pass(S)                                                       \
  template void record_mesh_pass(const PassCommonConfig &ccfg,                 \
                                 const MeshPassInfo<S> &info)

add_mesh_pass(DrawSet::DepthOnly);
add_mesh_pass(DrawSet::Opaque);

#undef add_mesh_pass

} // namespace ren
