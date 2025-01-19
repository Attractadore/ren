#include "MeshPass.hpp"
#include "../Batch.hpp"
#include "../CommandRecorder.hpp"
#include "../Profiler.hpp"
#include "../RenderGraph.hpp"
#include "../Scene.hpp"
#include "../core/Views.hpp"
#include "../glsl/MeshletCulling.h"
#include "../glsl/StreamScan.h"
#include "EarlyZ.vert.hpp"
#include "ExclusiveScanUint32.comp.hpp"
#include "InstanceCullingAndLOD.comp.hpp"
#include "MeshletCulling.comp.hpp"
#include "MeshletSorting.comp.hpp"
#include "Opaque.frag.hpp"
#include "PrepareBatch.comp.hpp"

#include <fmt/format.h>

namespace ren {

namespace {

struct CullingInfo {
  u32 draw_set = -1;
  NotNull<RgBufferId<glsl::MeshletDrawCommand> *> batch_commands;
  NotNull<RgBufferId<u32> *> batch_offsets;
  NotNull<RgBufferId<u32> *> batch_sizes;
  NotNull<RgBufferId<glsl::DispatchIndirectCommand> *> batch_prepare_commands;
};

void record_culling(const PassCommonConfig &ccfg, const MeshPassBaseInfo &info,
                    RgBuilder &rgb, const CullingInfo &cfg) {
  ren_prof_zone("Record culling");

  const DrawSetData &ds = info.gpu_scene->draw_sets[cfg.draw_set];
  const RgDrawSetData &rg_ds = info.rg_gpu_scene->draw_sets[cfg.draw_set];

  u32 num_batches = ds.batches.size();

  u32 num_instances = ds.size();

  u32 num_meshlets = 0;
  for (auto i : range(ds.batches.size())) {
    num_meshlets += ds.batches[i].num_meshlets;
  }

  u32 buckets_size = 0;
  std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
  for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
    bucket_offsets[bucket] = buckets_size;
    u32 bucket_stride = 1 << bucket;
    u32 bucket_size = std::min(num_instances, num_meshlets / bucket_stride);
    buckets_size += bucket_size;
  }

  auto meshlet_bucket_commands =
      rgb.create_buffer<glsl::DispatchIndirectCommand>({
          .name = "meshlet-bucket-commands-empty",
          .count = glsl::NUM_MESHLET_CULLING_BUCKETS,
          .init = glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1},
      });

  auto meshlet_bucket_sizes = rgb.create_buffer<u32>({
      .name = "meshlet-bucket-sizes-zero",
      .count = glsl::NUM_MESHLET_CULLING_BUCKETS,
      .init = 0,
  });

  auto meshlet_cull_data =
      rgb.create_buffer<glsl::MeshletCullData>({.count = buckets_size});

  *cfg.batch_sizes = rgb.create_buffer<u32>({
      .name = "batch-sizes-zero",
      .count = num_batches,
      .init = 0,
  });

  *cfg.batch_prepare_commands =
      rgb.create_buffer<glsl::DispatchIndirectCommand>({
          .name = "batch-prepare-commands-empty",
          .count = num_batches,
          .init = glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1},
      });

  auto num_commands = rgb.create_buffer<u32>({.init = 0});

  auto sort_command = rgb.create_buffer<glsl::DispatchIndirectCommand>({
      .name = "sort-command-empty",
      .init = glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1},
  });

  {
    auto pass = rgb.create_pass({"instance-culling-and-lod"});

    const SceneGraphicsSettings &settings = ccfg.scene->settings;

    u32 feature_mask = 0;
    if (settings.instance_frustum_culling) {
      feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
    }
    if (settings.lod_selection) {
      feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
    }
    feature_mask |= (u32)info.occlusion_culling_mode;

    float num_viewport_triangles =
        info.viewport.x * info.viewport.y / settings.lod_triangle_pixels;
    float lod_triangle_density = num_viewport_triangles / 4.0f;

    auto meshlet_bucket_offsets =
        ccfg.allocator->allocate<u32>(bucket_offsets.size());
    std::ranges::copy(bucket_offsets, meshlet_bucket_offsets.host_ptr);

    RgInstanceCullingAndLODArgs args = {
        .meshes = pass.read_buffer(info.rg_gpu_scene->meshes, CS_READ_BUFFER),
        .transform_matrices = pass.read_buffer(
            info.rg_gpu_scene->transform_matrices, CS_READ_BUFFER),
        .cull_data = pass.read_buffer(rg_ds.cull_data, CS_READ_BUFFER),
        .meshlet_bucket_commands =
            pass.write_buffer("meshlet-bucket-commands",
                              &meshlet_bucket_commands, CS_WRITE_BUFFER),
        .meshlet_bucket_offsets = meshlet_bucket_offsets.device_ptr,
        .meshlet_bucket_sizes = pass.write_buffer(
            "meshlet-bucket-sizes", &meshlet_bucket_sizes, CS_WRITE_BUFFER),
        .meshlet_cull_data = pass.write_buffer(
            "meshlet-cull-data", &meshlet_cull_data, CS_WRITE_BUFFER),
        .feature_mask = feature_mask,
        .num_instances = num_instances,
        .proj_view = get_projection_view_matrix(info.camera, info.viewport),
        .lod_triangle_density = lod_triangle_density,
        .lod_bias = settings.lod_bias,
    };

    if (info.occlusion_culling_mode == OcclusionCullingMode::SecondPhase) {
      ren_assert(info.hi_z);
      args.mesh_instance_visibility = pass.write_buffer(
          "new-mesh-instance-visibility",
          &info.rg_gpu_scene->mesh_instance_visibility, CS_READ_WRITE_BUFFER);
      args.hi_z =
          pass.read_texture(info.hi_z, CS_SAMPLE_TEXTURE, ccfg.samplers->hi_z);
    } else if (info.occlusion_culling_mode != OcclusionCullingMode::Disabled) {
      args.mesh_instance_visibility = pass.read_buffer(
          info.rg_gpu_scene->mesh_instance_visibility, CS_READ_BUFFER);
    }

    pass.dispatch_grid(ccfg.pipelines->instance_culling_and_lod, args,
                       num_instances);
  }

  auto unsorted_batch_commands = rgb.create_buffer<glsl::MeshletDrawCommand>(
      {.count = glsl::MAX_DRAW_MESHLETS});

  auto unsorted_batch_command_batch_ids =
      rgb.create_buffer<glsl_BatchId>({.count = glsl::MAX_DRAW_MESHLETS});

  {
    auto pass = rgb.create_pass({"meshlet-culling"});

    struct {
      Handle<ComputePipeline> pipeline;
      RgBufferToken<glsl::DispatchIndirectCommand> meshlet_bucket_commands;
      std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
    } rcs;

    rcs.pipeline = ccfg.pipelines->meshlet_culling;
    rcs.meshlet_bucket_commands =
        pass.read_buffer(meshlet_bucket_commands, INDIRECT_COMMAND_SRC_BUFFER);
    rcs.bucket_offsets = bucket_offsets;

    RgMeshletCullingArgs args = {
        .meshes = pass.read_buffer(info.rg_gpu_scene->meshes, CS_READ_BUFFER),
        .transform_matrices = pass.read_buffer(
            info.rg_gpu_scene->transform_matrices, CS_READ_BUFFER),
        .bucket_cull_data = pass.read_buffer(meshlet_cull_data, CS_READ_BUFFER),
        .bucket_size = pass.read_buffer(meshlet_bucket_sizes, CS_READ_BUFFER),
        .batch_sizes = pass.write_buffer("batch-sizes", cfg.batch_sizes.get(),
                                         CS_ATOMIC_BUFFER),
        .batch_prepare_commands = pass.write_buffer(
            "batch-prepare-commands", cfg.batch_prepare_commands.get(),
            CS_ATOMIC_BUFFER),
        .commands =
            pass.write_buffer("unsorted-batch-commands",
                              &unsorted_batch_commands, CS_WRITE_BUFFER),
        .command_batch_ids = pass.write_buffer(
            "unsorted-batch-command-batch-ids",
            &unsorted_batch_command_batch_ids, CS_WRITE_BUFFER),
        .num_commands = pass.write_buffer("unsorted-batch-command-count",
                                          &num_commands, CS_ATOMIC_BUFFER),
        .sort_command =
            pass.write_buffer("sort-command", &sort_command, CS_ATOMIC_BUFFER),
        .proj_view = get_projection_view_matrix(info.camera, info.viewport),
        .eye = info.camera.position,
    };

    const SceneGraphicsSettings &settings = ccfg.scene->settings;

    if (settings.meshlet_cone_culling) {
      args.feature_mask |= glsl::MESHLET_CULLING_CONE_BIT;
    }
    if (settings.meshlet_frustum_culling) {
      args.feature_mask |= glsl::MESHLET_CULLING_FRUSTUM_BIT;
    }
    if (settings.meshlet_occlusion_culling and info.hi_z) {
      args.feature_mask |= glsl::MESHLET_CULLING_OCCLUSION_BIT;
      args.hi_z =
          pass.read_texture(info.hi_z, CS_SAMPLE_TEXTURE, ccfg.samplers->hi_z);
    }

    pass.set_compute_callback(
        [rcs, args](Renderer &, const RgRuntime &rg, ComputePass &pass) {
          pass.set_descriptor_heaps(rg.get_resource_descriptor_heap(),
                                    rg.get_sampler_descriptor_heap());
          pass.bind_compute_pipeline(rcs.pipeline);
          auto pc = to_push_constants(rg, args);
          DevicePtr<glsl::MeshletCullData> base_cull_data = pc.bucket_cull_data;
          DevicePtr<u32> base_bucket_size = pc.bucket_size;
          for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
            pc.bucket_cull_data = base_cull_data + rcs.bucket_offsets[bucket];
            pc.bucket_size = base_bucket_size + bucket;
            pc.bucket = bucket;
            pass.set_push_constants(pc);
            pass.dispatch_indirect(
                rg.get_buffer(rcs.meshlet_bucket_commands).slice(bucket));
          }
        });
  }

  *cfg.batch_offsets = rgb.create_buffer<u32>({.count = num_batches});

  {
    auto block_sums = rgb.create_buffer<u32>(
        {.count = glsl::get_stream_scan_block_sum_count(num_batches)});

    auto scan_num_started =
        rgb.create_buffer<u32>({.name = "scan-num-started-zero", .init = 0});

    auto scan_num_finished =
        rgb.create_buffer<u32>({.name = "scan-num-finished-zero", .init = 0});

    auto pass = rgb.create_pass({"batch-sizes-scan"});

    RgStreamScanArgs args = {
        .src = pass.read_buffer(*cfg.batch_sizes, CS_READ_BUFFER),
        .block_sums = pass.write_buffer("scan-block-sums", &block_sums,
                                        CS_READ_WRITE_BUFFER),
        .dst = pass.write_buffer("batch-offsets", cfg.batch_offsets.get(),
                                 CS_WRITE_BUFFER),
        .num_started = pass.write_buffer("scan-num-started", &scan_num_started,
                                         CS_ATOMIC_BUFFER),
        .num_finished = pass.write_buffer("scan-num-finished",
                                          &scan_num_finished, CS_ATOMIC_BUFFER),
        .count = num_batches,
    };

    pass.dispatch_grid(ccfg.pipelines->exclusive_scan_uint32, args,
                       num_batches);
  }

  *cfg.batch_commands = rgb.create_buffer<glsl::MeshletDrawCommand>(
      {.count = glsl::MAX_DRAW_MESHLETS});

  {
    RgBufferId<u32> batch_out_offsets =
        rgb.create_buffer<u32>({.count = num_batches});

    rgb.copy_buffer(*cfg.batch_offsets, "init-batch-out-offsets",
                    &batch_out_offsets);

    auto pass = rgb.create_pass({"meshlet-sorting"});

    RgMeshletSortingArgs args = {
        .num_commands = pass.read_buffer(num_commands, CS_READ_BUFFER),
        .batch_out_offsets = pass.write_buffer(
            "batch-out-offsets", &batch_out_offsets, CS_ATOMIC_BUFFER),
        .unsorted_commands =
            pass.read_buffer(unsorted_batch_commands, CS_READ_BUFFER),
        .unsorted_command_batch_ids =
            pass.read_buffer(unsorted_batch_command_batch_ids, CS_READ_BUFFER),
        .commands = pass.write_buffer(
            "batch-commands", cfg.batch_commands.get(), CS_WRITE_BUFFER),
    };

    pass.dispatch_indirect(ccfg.pipelines->meshlet_sorting, args, sort_command);
  }
} // namespace

auto get_render_pass_args(const SceneData &, const DepthOnlyMeshPassInfo &info,
                          RgPassBuilder &pass) {
  const RgGpuScene &gpu_scene = *info.base.rg_gpu_scene;
  return RgEarlyZArgs{
      .meshes = pass.read_buffer(gpu_scene.meshes, VS_READ_BUFFER),
      .mesh_instances =
          pass.read_buffer(gpu_scene.mesh_instances, VS_READ_BUFFER),
      .transform_matrices =
          pass.read_buffer(gpu_scene.transform_matrices, VS_READ_BUFFER),
      .proj_view =
          get_projection_view_matrix(info.base.camera, info.base.viewport),
  };
}

auto get_render_pass_args(const SceneData &scene,
                          const OpaqueMeshPassInfo &info, RgPassBuilder &pass) {
  const RgGpuScene &gpu_scene = *info.base.rg_gpu_scene;
  return RgOpaqueArgs{
      .meshes = pass.read_buffer(gpu_scene.meshes, VS_READ_BUFFER),
      .mesh_instances =
          pass.read_buffer(gpu_scene.mesh_instances, VS_READ_BUFFER),
      .transform_matrices =
          pass.read_buffer(gpu_scene.transform_matrices, VS_READ_BUFFER),
      .materials = pass.read_buffer(gpu_scene.materials, FS_READ_BUFFER),
      .directional_lights =
          pass.read_buffer(gpu_scene.directional_lights, FS_READ_BUFFER),
      .num_directional_lights = u32(scene.directional_lights.size()),
      .proj_view =
          get_projection_view_matrix(info.base.camera, info.base.viewport),
      .eye = info.base.camera.position,
      .exposure = pass.read_texture(info.exposure, FS_SAMPLE_TEXTURE,
                                    info.exposure_temporal_layer),
  };
}

struct RenderPassInfo {
  RgBufferId<glsl::MeshletDrawCommand> batch_commands;
  RgBufferId<u32> batch_offsets;
  RgBufferId<u32> batch_sizes;
  RgBufferId<glsl::DispatchIndirectCommand> batch_prepare_commands;
};

template <DrawSet S>
void record_render_pass(const PassCommonConfig &ccfg,
                        const MeshPassInfo<S> &info,
                        const RenderPassInfo &cfg) {
  ren_prof_zone("Record render pass");

  u32 draw_set = get_draw_set_index(S);

  const DrawSetData &ds = info.base.gpu_scene->draw_sets[draw_set];
  const RgDrawSetData &rg_ds = info.base.rg_gpu_scene->draw_sets[draw_set];

  const char *pass_type = "";
  if (info.base.occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
    pass_type = "-first-phase";
  } else if (info.base.occlusion_culling_mode ==
             OcclusionCullingMode::SecondPhase) {
    pass_type = "-second-phase";
  }

  RgBufferId<glsl::DrawIndexedIndirectCommand> commands =
      ccfg.rgb->create_buffer<glsl::DrawIndexedIndirectCommand>(
          {.count = glsl::MAX_DRAW_MESHLETS});

  for (glsl_BatchId batch : range(ds.batches.size())) {
    {
      auto pass = ccfg.rgb->create_pass({fmt::format(
          "{}{}-prepare-batch-{}", info.base.pass_name, pass_type, batch)});

      RgPrepareBatchArgs args = {
          .batch_offset =
              pass.read_buffer(cfg.batch_offsets, CS_READ_BUFFER, batch),
          .batch_size =
              pass.read_buffer(cfg.batch_sizes, CS_READ_BUFFER, batch),
          .command_descs = pass.read_buffer(cfg.batch_commands, CS_READ_BUFFER),
          .commands = pass.write_buffer(fmt::format("{}{}-batch-{}-commands",
                                                    info.base.pass_name,
                                                    pass_type, batch),
                                        &commands, CS_WRITE_BUFFER),
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
      ColorAttachmentOperations ops = info.base.color_attachment_ops[i];
      if (info.base.occlusion_culling_mode ==
              OcclusionCullingMode::SecondPhase or
          batch > 0) {
        ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
      }
      std::tie(*color_attachment, std::ignore) = pass.write_color_attachment(
          info.base.color_attachment_names[i], *color_attachment, ops);
    }

    if (*info.base.depth_attachment) {
      if (info.base.depth_attachment_ops.store == VK_ATTACHMENT_STORE_OP_NONE) {
        pass.read_depth_attachment(*info.base.depth_attachment);
      } else {
        DepthAttachmentOperations ops = info.base.depth_attachment_ops;
        if (info.base.occlusion_culling_mode ==
                OcclusionCullingMode::SecondPhase or
            batch > 0) {
          ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        std::tie(*info.base.depth_attachment, std::ignore) =
            pass.write_depth_attachment(info.base.depth_attachment_name,
                                        *info.base.depth_attachment, ops);
      }
    }

    struct {
      BatchDesc batch;
      RgBufferToken<glsl::DrawIndexedIndirectCommand> commands;
      RgBufferToken<u32> batch_sizes;
    } rcs;

    rcs.batch = info.base.gpu_scene->draw_sets[draw_set].batches[batch].desc;
    rcs.commands = pass.read_buffer(commands, INDIRECT_COMMAND_SRC_BUFFER);
    rcs.batch_sizes =
        pass.read_buffer(cfg.batch_sizes, INDIRECT_COMMAND_SRC_BUFFER, batch);

    auto args = get_render_pass_args(*ccfg.scene, info, pass);

    pass.set_graphics_callback([rcs, args](Renderer &, const RgRuntime &rg,
                                           RenderPass &render_pass) {
      render_pass.set_descriptor_heaps(rg.get_resource_descriptor_heap(),
                                       rg.get_sampler_descriptor_heap());
      render_pass.bind_graphics_pipeline(rcs.batch.pipeline);
      render_pass.bind_index_buffer(rcs.batch.index_buffer,
                                    VK_INDEX_TYPE_UINT8_EXT);
      rg.set_push_constants(render_pass, args);
      render_pass.draw_indexed_indirect_count(rg.get_buffer(rcs.commands),
                                              rg.get_buffer(rcs.batch_sizes));
    });
  }
}

} // namespace

template <DrawSet S>
void record_mesh_pass(const PassCommonConfig &ccfg,
                      const MeshPassInfo<S> &info) {
  ren_prof_zone("MeshPass::record");
#if REN_RG_DEBUG
  ren_prof_zone_text(info.base.pass_name);
#endif

  RgBufferId<glsl::MeshletDrawCommand> batch_commands;
  RgBufferId<u32> batch_offsets, batch_sizes;
  RgBufferId<glsl::DispatchIndirectCommand> batch_prepare_commands;
  record_culling(ccfg, info.base, *ccfg.rgb,
                 CullingInfo{
                     .draw_set = get_draw_set_index(S),
                     .batch_commands = &batch_commands,
                     .batch_offsets = &batch_offsets,
                     .batch_sizes = &batch_sizes,
                     .batch_prepare_commands = &batch_prepare_commands,
                 });

  record_render_pass(ccfg, info,
                     RenderPassInfo{
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
