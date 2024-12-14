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
          .heap = BufferHeap::Static,
          .size = glsl::NUM_MESHLET_CULLING_BUCKETS,
      });

  auto meshlet_bucket_sizes = rgb.create_buffer<u32>({
      .heap = BufferHeap::Static,
      .size = glsl::NUM_MESHLET_CULLING_BUCKETS,
  });

  auto meshlet_cull_data = rgb.create_buffer<glsl::MeshletCullData>({
      .heap = BufferHeap::Static,
      .size = buckets_size,
  });

  u32 num_batches = ds.batches.size();

  *cfg.batch_sizes = rgb.create_buffer<u32>({
      .heap = BufferHeap::Static,
      .count = num_batches,
  });

  *cfg.batch_prepare_commands =
      rgb.create_buffer<glsl::DispatchIndirectCommand>({
          .heap = BufferHeap::Static,
          .count = num_batches,
      });

  RgBufferId<u32> num_commands =
      rgb.create_buffer<u32>({.heap = BufferHeap::Static, .count = 1});

  RgBufferId<glsl::DispatchIndirectCommand> sort_command =
      rgb.create_buffer<glsl::DispatchIndirectCommand>({
          .heap = BufferHeap::Static,
          .count = 1,
      });

  RgBufferId<u32> scan_num_started =
      rgb.create_buffer<u32>({.heap = BufferHeap::Static, .count = 1});

  RgBufferId<u32> scan_num_finished =
      rgb.create_buffer<u32>({.heap = BufferHeap::Static, .count = 1});

  {
    auto pass = rgb.create_pass({"init-culling"});

    struct {
      RgUntypedBufferToken meshlet_bucket_commands;
      RgUntypedBufferToken meshlet_bucket_sizes;
      RgBufferToken<u32> batch_sizes;
      RgBufferToken<glsl::DispatchIndirectCommand> batch_prepare_commands;
      RgBufferToken<u32> num_commands;
      RgBufferToken<glsl::DispatchIndirectCommand> sort_command;
      RgBufferToken<u32> scan_num_started;
      RgBufferToken<u32> scan_num_finished;
      u32 num_batches = 0;
    } rcs;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("init-meshlet-bucket-commands",
                          meshlet_bucket_commands, TRANSFER_DST_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("init-meshlet-bucket-sizes", meshlet_bucket_sizes,
                          TRANSFER_DST_BUFFER);

    std::tie(*cfg.batch_sizes, rcs.batch_sizes) = pass.write_buffer(
        "init-batch-sizes", *cfg.batch_sizes, TRANSFER_DST_BUFFER);

    std::tie(*cfg.batch_prepare_commands, rcs.batch_prepare_commands) =
        pass.write_buffer("init-batch-prepare-commands",
                          *cfg.batch_prepare_commands, TRANSFER_DST_BUFFER);

    std::tie(num_commands, rcs.num_commands) = pass.write_buffer(
        "init-command-count", num_commands, TRANSFER_DST_BUFFER);
    std::tie(sort_command, rcs.sort_command) = pass.write_buffer(
        "init-sort-command", sort_command, TRANSFER_DST_BUFFER);

    std::tie(scan_num_started, rcs.scan_num_started) = pass.write_buffer(
        "init-scan-num-started", scan_num_started, TRANSFER_DST_BUFFER);
    std::tie(scan_num_finished, rcs.scan_num_finished) = pass.write_buffer(
        "init-scan-num-finished", scan_num_finished, TRANSFER_DST_BUFFER);

    rcs.num_batches = num_batches;

    pass.set_callback([rcs](Renderer &, const RgRuntime &rg,
                            CommandRecorder &cmd) {
      std::array<glsl::DispatchIndirectCommand,
                 glsl::NUM_MESHLET_CULLING_BUCKETS>
          commands;
      std::ranges::fill(commands,
                        glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1});
      cmd.update_buffer(rg.get_buffer(rcs.meshlet_bucket_commands), commands);

      cmd.fill_buffer(rg.get_buffer(rcs.meshlet_bucket_sizes), 0);

      cmd.fill_buffer(BufferView(rg.get_buffer(rcs.batch_sizes)), 0);

      auto batch_prepare_commands =
          rg.allocate<glsl::DispatchIndirectCommand>(rcs.num_batches);
      std::ranges::fill_n(
          batch_prepare_commands.host_ptr, rcs.num_batches,
          glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1});
      cmd.copy_buffer(batch_prepare_commands.slice,
                      rg.get_buffer(rcs.batch_prepare_commands));

      cmd.fill_buffer(BufferView(rg.get_buffer(rcs.num_commands)), 0);
      cmd.update_buffer(BufferView(rg.get_buffer(rcs.sort_command)),
                        glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1});

      cmd.fill_buffer(BufferView(rg.get_buffer(rcs.scan_num_started)), 0);
      cmd.fill_buffer(BufferView(rg.get_buffer(rcs.scan_num_finished)), 0);
    });
  }

  {
    auto pass = rgb.create_pass({"instance-culling-and-lod"});

    struct {
      Handle<ComputePipeline> pipeline;
      uint num_instances;
    } rcs;

    rcs.pipeline = ccfg.pipelines->instance_culling_and_lod;
    rcs.num_instances = num_instances;

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

    pass.set_compute_callback(
        [rcs, args](Renderer &, const RgRuntime &rg, ComputePass &cmd) {
          cmd.bind_compute_pipeline(rcs.pipeline);
          cmd.bind_descriptor_sets({rg.get_texture_set()});
          rg.set_push_constants(cmd, args);
          cmd.dispatch_threads(rcs.num_instances,
                               glsl::INSTANCE_CULLING_AND_LOD_THREADS);
        });
  }

  RgBufferId<glsl::MeshletDrawCommand> unsorted_batch_commands =
      rgb.create_buffer<glsl::MeshletDrawCommand>({
          .heap = BufferHeap::Static,
          .count = glsl::MAX_DRAW_MESHLETS,
      });

  RgBufferId<BatchId> unsorted_batch_command_batch_ids =
      rgb.create_buffer<BatchId>({
          .heap = BufferHeap::Static,
          .count = glsl::MAX_DRAW_MESHLETS,
      });

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
          pass.bind_compute_pipeline(rcs.pipeline);
          pass.bind_descriptor_sets({rg.get_texture_set()});
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

  *cfg.batch_offsets = rgb.create_buffer<u32>({
      .heap = BufferHeap::Static,
      .count = num_batches,
  });

  {
    auto pass = rgb.create_pass({"batch-sizes-scan"});

    RgBufferId<u32> block_sums = rgb.create_buffer<u32>({
        .heap = BufferHeap::Static,
        .count = glsl::get_stream_scan_block_sum_count(num_batches),
    });

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

    pass.set_compute_callback(
        [pipeline = ccfg.pipelines->exclusive_scan_uint32,
         args](Renderer &, const RgRuntime &rg, ComputePass &cmd) {
          cmd.bind_compute_pipeline(pipeline);
          rg.set_push_constants(cmd, args);
          cmd.dispatch_threads(args.count, glsl::SCAN_BLOCK_ELEMS);
        });
  }

  RgBufferId<u32> batch_out_offsets = rgb.create_buffer<u32>({
      .heap = BufferHeap::Static,
      .count = num_batches,
  });

  {
    auto pass = rgb.create_pass({"init-meshlet-sorting"});

    struct {
      RgBufferToken<u32> src;
      RgBufferToken<u32> dst;
    } rcs;

    rcs.src = pass.read_buffer(*cfg.batch_offsets, TRANSFER_SRC_BUFFER);
    std::tie(batch_out_offsets, rcs.dst) = pass.write_buffer(
        "init-batch-out-offsets", batch_out_offsets, TRANSFER_DST_BUFFER);

    pass.set_callback(
        [rcs](Renderer &, const RgRuntime &rg, CommandRecorder &cmd) {
          cmd.copy_buffer(rg.get_buffer(rcs.src), rg.get_buffer(rcs.dst));
        });
  }

  {
    auto pass = rgb.create_pass({"meshlet-sorting"});

    *cfg.batch_commands = rgb.create_buffer<glsl::MeshletDrawCommand>({
        .heap = BufferHeap::Static,
        .count = glsl::MAX_DRAW_MESHLETS,
    });

    struct {
      Handle<ComputePipeline> pipeline;
      RgBufferToken<glsl::DispatchIndirectCommand> sort_command;
    } rcs;

    rcs.pipeline = ccfg.pipelines->meshlet_sorting;
    rcs.sort_command =
        pass.read_buffer(sort_command, INDIRECT_COMMAND_SRC_BUFFER);

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

    pass.set_compute_callback(
        [rcs, args](Renderer &, const RgRuntime &rg, ComputePass &pass) {
          pass.bind_compute_pipeline(rcs.pipeline);
          rg.set_push_constants(pass, args);
          pass.dispatch_indirect(rg.get_buffer(rcs.sort_command));
        });
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
      .normal_matrices =
          pass.read_buffer(gpu_scene.normal_matrices, VS_READ_BUFFER),
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
      ccfg.rgb->create_buffer<glsl::DrawIndexedIndirectCommand>({
          .heap = BufferHeap::Static,
          .count = glsl::MAX_DRAW_MESHLETS,
      });

  for (BatchId batch : range(ds.batches.size())) {
    {
      auto pass = ccfg.rgb->create_pass({fmt::format(
          "{}{}-prepare-batch-{}", info.base.pass_name, pass_type, batch)});

      RgBufferToken<glsl::DispatchIndirectCommand> batch_prepare_commands =
          pass.read_buffer(cfg.batch_prepare_commands,
                           INDIRECT_COMMAND_SRC_BUFFER, batch);

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

      pass.set_compute_callback(
          [pipeline = ccfg.pipelines->prepare_batch, batch_prepare_commands,
           batch, args](Renderer &, const RgRuntime &rg, ComputePass &pass) {
            pass.bind_compute_pipeline(pipeline);
            rg.set_push_constants(pass, args);
            pass.dispatch_indirect(rg.get_buffer(batch_prepare_commands));
          });
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
      render_pass.bind_graphics_pipeline(rcs.batch.pipeline);
      if (S == DrawSet::Opaque) {
        render_pass.bind_descriptor_sets({rg.get_texture_set()});
      }
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
