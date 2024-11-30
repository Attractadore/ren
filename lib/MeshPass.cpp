#include "MeshPass.hpp"
#include "Batch.hpp"
#include "CommandRecorder.hpp"
#include "Profiler.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.h"
#include "glsl/MeshletCullingPass.h"
#include "glsl/MeshletSorting.h"
#include "glsl/OpaquePass.h"
#include "glsl/PrepareBatch.h"
#include "glsl/StreamScan.h"

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
      DevicePtr<glsl::InstanceCullingAndLODPassUniforms> uniforms;
      RgBufferToken<glsl::Mesh> meshes;
      RgBufferToken<glm::mat4x3> transform_matrices;
      RgBufferToken<glsl::InstanceCullData> instance_cull_data;
      u32 num_instances;
      RgBufferToken<glsl::DispatchIndirectCommand> meshlet_bucket_commands;
      RgBufferToken<u32> meshlet_bucket_sizes;
      RgBufferToken<glsl::MeshletCullData> meshlet_cull_data;
      RgBufferToken<MeshInstanceVisibilityMask> mesh_instance_visibility;
      RgTextureToken hi_z;
    } rcs;

    rcs.pipeline = ccfg.pipelines->instance_culling_and_lod;

    rcs.meshes = pass.read_buffer(info.rg_gpu_scene->meshes, CS_READ_BUFFER);

    rcs.transform_matrices =
        pass.read_buffer(info.rg_gpu_scene->transform_matrices, CS_READ_BUFFER);

    rcs.instance_cull_data = pass.read_buffer(rg_ds.cull_data, CS_READ_BUFFER);
    rcs.num_instances = num_instances;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("meshlet-bucket-commands", meshlet_bucket_commands,
                          CS_WRITE_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("meshlet-bucket-sizes", meshlet_bucket_sizes,
                          CS_WRITE_BUFFER);

    std::tie(meshlet_cull_data, rcs.meshlet_cull_data) = pass.write_buffer(
        "meshlet-cull-data", meshlet_cull_data, CS_WRITE_BUFFER);

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
    i32 lod_bias = settings.lod_bias;

    auto [uniforms, uniforms_ptr, _2] =
        ccfg.allocator->allocate<glsl::InstanceCullingAndLODPassUniforms>(1);
    *uniforms = {
        .feature_mask = feature_mask,
        .num_instances = num_instances,
        .proj_view = get_projection_view_matrix(info.camera, info.viewport),
        .lod_triangle_density = lod_triangle_density,
        .lod_bias = lod_bias,
        .meshlet_bucket_offsets = bucket_offsets,
    };
    rcs.uniforms = uniforms_ptr;

    if (info.occlusion_culling_mode == OcclusionCullingMode::SecondPhase) {
      ren_assert(info.hi_z);
      std::tie(info.rg_gpu_scene->mesh_instance_visibility,
               rcs.mesh_instance_visibility) =
          pass.write_buffer("new-mesh-instance-visibility",
                            info.rg_gpu_scene->mesh_instance_visibility,
                            CS_READ_WRITE_BUFFER);
      rcs.hi_z =
          pass.read_texture(info.hi_z, CS_SAMPLE_TEXTURE, ccfg.samplers->hi_z);
    } else if (info.occlusion_culling_mode != OcclusionCullingMode::Disabled) {
      rcs.mesh_instance_visibility = pass.read_buffer(
          info.rg_gpu_scene->mesh_instance_visibility, CS_READ_BUFFER);
    }

    pass.set_compute_callback([rcs](Renderer &, const RgRuntime &rg,
                                    ComputePass &cmd) {
      cmd.bind_compute_pipeline(rcs.pipeline);
      cmd.bind_descriptor_sets({rg.get_texture_set()});
      ren_assert(rcs.uniforms);
      ren_assert(rcs.instance_cull_data);
      cmd.set_push_constants(glsl::InstanceCullingAndLODPassArgs{
          .ub = rcs.uniforms,
          .meshes = rg.get_buffer_device_ptr(rcs.meshes),
          .transform_matrices =
              rg.get_buffer_device_ptr(rcs.transform_matrices),
          .cull_data = rg.get_buffer_device_ptr(rcs.instance_cull_data),
          .meshlet_bucket_commands =
              rg.get_buffer_device_ptr(rcs.meshlet_bucket_commands),
          .meshlet_bucket_sizes =
              rg.get_buffer_device_ptr(rcs.meshlet_bucket_sizes),
          .meshlet_cull_data = rg.get_buffer_device_ptr(rcs.meshlet_cull_data),
          .mesh_instance_visibility =
              rg.try_get_buffer_device_ptr(rcs.mesh_instance_visibility),
          .hi_z = glsl::SampledTexture2D(
              rg.try_get_sampled_texture_descriptor(rcs.hi_z)),
      });
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
      RgBufferToken<glsl::Mesh> meshes;
      RgBufferToken<glm::mat4x3> transform_matrices;
      RgBufferToken<glsl::DispatchIndirectCommand> meshlet_bucket_commands;
      RgBufferToken<glsl::MeshletCullData> meshlet_cull_data;
      RgBufferToken<u32> meshlet_bucket_sizes;
      RgBufferToken<u32> batch_sizes;
      RgBufferToken<glsl::DispatchIndirectCommand> batch_prepare_commands;
      RgBufferToken<BatchId> command_batch_ids;
      RgBufferToken<glsl::MeshletDrawCommand> commands;
      RgBufferToken<u32> num_commands;
      RgBufferToken<glsl::DispatchIndirectCommand> sort_command;
      RgTextureToken hi_z;
      DevicePtr<glm::mat4> proj_view;
      u32 feature_mask;
      std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
      glm::vec3 eye;
    } rcs;

    rcs.pipeline = ccfg.pipelines->meshlet_culling;

    rcs.meshes = pass.read_buffer(info.rg_gpu_scene->meshes, CS_READ_BUFFER);

    rcs.transform_matrices =
        pass.read_buffer(info.rg_gpu_scene->transform_matrices, CS_READ_BUFFER);

    rcs.meshlet_bucket_commands =
        pass.read_buffer(meshlet_bucket_commands, INDIRECT_COMMAND_SRC_BUFFER);

    rcs.meshlet_cull_data = pass.read_buffer(meshlet_cull_data, CS_READ_BUFFER);

    rcs.meshlet_bucket_sizes =
        pass.read_buffer(meshlet_bucket_sizes, CS_READ_BUFFER);

    std::tie(*cfg.batch_sizes, rcs.batch_sizes) =
        pass.write_buffer("batch-sizes", *cfg.batch_sizes, CS_ATOMIC_BUFFER);
    std::tie(*cfg.batch_prepare_commands, rcs.batch_prepare_commands) =
        pass.write_buffer("batch-prepare-commands", *cfg.batch_prepare_commands,
                          CS_ATOMIC_BUFFER);

    std::tie(unsorted_batch_commands, rcs.commands) = pass.write_buffer(
        "unsorted-batch-commands", unsorted_batch_commands, CS_WRITE_BUFFER);
    std::tie(unsorted_batch_command_batch_ids, rcs.command_batch_ids) =
        pass.write_buffer("unsorted-batch-command-batch-ids",
                          unsorted_batch_command_batch_ids, CS_WRITE_BUFFER);
    std::tie(num_commands, rcs.num_commands) = pass.write_buffer(
        "unsorted-batch-command-count", num_commands, CS_ATOMIC_BUFFER);
    std::tie(sort_command, rcs.sort_command) =
        pass.write_buffer("sort-command", sort_command, CS_ATOMIC_BUFFER);

    auto [proj_view, proj_view_ptr, _] = ccfg.allocator->allocate<glm::mat4>(1);
    *proj_view = get_projection_view_matrix(info.camera, info.viewport);
    rcs.proj_view = proj_view_ptr;

    const SceneGraphicsSettings &settings = ccfg.scene->settings;

    rcs.feature_mask = 0;
    if (settings.meshlet_cone_culling) {
      rcs.feature_mask |= glsl::MESHLET_CULLING_CONE_BIT;
    }
    if (settings.meshlet_frustum_culling) {
      rcs.feature_mask |= glsl::MESHLET_CULLING_FRUSTUM_BIT;
    }
    if (settings.meshlet_occlusion_culling and info.hi_z) {
      rcs.feature_mask |= glsl::MESHLET_CULLING_OCCLUSION_BIT;
      rcs.hi_z =
          pass.read_texture(info.hi_z, CS_SAMPLE_TEXTURE, ccfg.samplers->hi_z);
    }

    rcs.bucket_offsets = bucket_offsets;
    rcs.eye = info.camera.position;

    pass.set_compute_callback(
        [rcs](Renderer &, const RgRuntime &rg, ComputePass &pass) {
          pass.bind_compute_pipeline(rcs.pipeline);
          pass.bind_descriptor_sets({rg.get_texture_set()});
          for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
            pass.set_push_constants(glsl::MeshletCullingPassArgs{
                .meshes = rg.get_buffer_device_ptr(rcs.meshes),
                .transform_matrices =
                    rg.get_buffer_device_ptr(rcs.transform_matrices),
                .bucket_cull_data =
                    rg.get_buffer_device_ptr(rcs.meshlet_cull_data) +
                    rcs.bucket_offsets[bucket],
                .bucket_size =
                    rg.get_buffer_device_ptr(rcs.meshlet_bucket_sizes) + bucket,
                .batch_sizes = rg.get_buffer_device_ptr(rcs.batch_sizes),
                .batch_prepare_commands =
                    rg.get_buffer_device_ptr(rcs.batch_prepare_commands),
                .commands = rg.get_buffer_device_ptr(rcs.commands),
                .command_batch_ids =
                    rg.get_buffer_device_ptr(rcs.command_batch_ids),
                .num_commands = rg.get_buffer_device_ptr(rcs.num_commands),
                .sort_command = rg.get_buffer_device_ptr(rcs.sort_command),
                .proj_view = rcs.proj_view,
                .feature_mask = rcs.feature_mask,
                .bucket = bucket,
                .eye = rcs.eye,
                .hi_z = glsl::SampledTexture2D(
                    rg.try_get_sampled_texture_descriptor(rcs.hi_z)),
            });
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

    struct {
      Handle<ComputePipeline> pipeline;
      RgBufferToken<u32> batch_sizes;
      RgBufferToken<u32> block_sums;
      RgBufferToken<u32> batch_offsets;
      RgBufferToken<u32> num_started;
      RgBufferToken<u32> num_finished;
      u32 num_batches = 0;
    } rcs;

    rcs.pipeline = ccfg.pipelines->exclusive_scan_uint32;

    rcs.batch_sizes = pass.read_buffer(*cfg.batch_sizes, CS_READ_BUFFER);

    RgBufferId<u32> block_sums = rgb.create_buffer<u32>({
        .heap = BufferHeap::Static,
        .count = glsl::get_stream_scan_block_sum_count(num_batches),
    });

    std::tie(std::ignore, rcs.block_sums) =
        pass.write_buffer("scan-block-sums", block_sums, CS_READ_WRITE_BUFFER);

    std::tie(*cfg.batch_offsets, rcs.batch_offsets) =
        pass.write_buffer("batch-offsets", *cfg.batch_offsets, CS_WRITE_BUFFER);

    std::tie(std::ignore, rcs.num_started) = pass.write_buffer(
        "scan-num-started", scan_num_started, CS_ATOMIC_BUFFER);
    std::tie(std::ignore, rcs.num_finished) = pass.write_buffer(
        "scan-num-finished", scan_num_finished, CS_ATOMIC_BUFFER);

    rcs.num_batches = num_batches;

    pass.set_compute_callback(
        [rcs](Renderer &, const RgRuntime &rg, ComputePass &pass) {
          pass.bind_compute_pipeline(rcs.pipeline);
          pass.set_push_constants(glsl::StreamScanArgs<u32>{
              .src = rg.get_buffer_device_ptr(rcs.batch_sizes),
              .block_sums = rg.get_buffer_device_ptr(rcs.block_sums),
              .dst = rg.get_buffer_device_ptr(rcs.batch_offsets),
              .num_started = rg.get_buffer_device_ptr(rcs.num_started),
              .num_finished = rg.get_buffer_device_ptr(rcs.num_finished),
              .count = rcs.num_batches,
          });
          pass.dispatch_threads(rcs.num_batches, glsl::SCAN_BLOCK_ELEMS);
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
      RgBufferToken<u32> num_commands;
      RgBufferToken<glsl::DispatchIndirectCommand> sort_command;
      RgBufferToken<u32> batch_out_offsets;
      RgBufferToken<glsl::MeshletDrawCommand> unsorted_commands;
      RgBufferToken<BatchId> unsorted_command_batch_ids;
      RgBufferToken<glsl::MeshletDrawCommand> commands;
    } rcs;

    rcs.pipeline = ccfg.pipelines->meshlet_sorting;
    rcs.num_commands = pass.read_buffer(num_commands, CS_READ_BUFFER);
    rcs.sort_command =
        pass.read_buffer(sort_command, INDIRECT_COMMAND_SRC_BUFFER);
    std::tie(std::ignore, rcs.batch_out_offsets) = pass.write_buffer(
        "batch-out-offsets", batch_out_offsets, CS_ATOMIC_BUFFER);
    rcs.unsorted_commands =
        pass.read_buffer(unsorted_batch_commands, CS_READ_BUFFER);
    rcs.unsorted_command_batch_ids =
        pass.read_buffer(unsorted_batch_command_batch_ids, CS_READ_BUFFER);
    std::tie(*cfg.batch_commands, rcs.commands) = pass.write_buffer(
        "batch-commands", *cfg.batch_commands, CS_WRITE_BUFFER);

    pass.set_compute_callback([rcs](Renderer &, const RgRuntime &rg,
                                    ComputePass &pass) {
      pass.bind_compute_pipeline(rcs.pipeline);
      pass.set_push_constants(glsl::MeshletSortingArgs{
          .num_commands = rg.get_buffer_device_ptr(rcs.num_commands),
          .batch_out_offsets = rg.get_buffer_device_ptr(rcs.batch_out_offsets),
          .unsorted_commands = rg.get_buffer_device_ptr(rcs.unsorted_commands),
          .unsorted_command_batch_ids =
              rg.get_buffer_device_ptr(rcs.unsorted_command_batch_ids),
          .commands = rg.get_buffer_device_ptr(rcs.commands),
      });
      pass.dispatch_indirect(rg.get_buffer(rcs.sort_command));
    });
  }
}

template <DrawSet S> struct RenderPassResources;

template <> struct RenderPassResources<DrawSet::DepthOnly> {
  RgBufferToken<glsl::Mesh> meshes;
  RgBufferToken<glsl::MeshInstance> mesh_instances;
  RgBufferToken<glm::mat4x3> transform_matrices;
  glm::mat4 proj_view;
};

using DepthOnlyRenderPassResources = RenderPassResources<DrawSet::DepthOnly>;

auto get_render_pass_resources(const SceneData &,
                               const DepthOnlyMeshPassInfo &info,
                               RgPassBuilder &pass)
    -> DepthOnlyRenderPassResources {
  DepthOnlyRenderPassResources rcs;

  const RgGpuScene &gpu_scene = *info.base.rg_gpu_scene;

  rcs.meshes = pass.read_buffer(gpu_scene.meshes, VS_READ_BUFFER);
  rcs.mesh_instances =
      pass.read_buffer(gpu_scene.mesh_instances, VS_READ_BUFFER);
  rcs.transform_matrices =
      pass.read_buffer(gpu_scene.transform_matrices, VS_READ_BUFFER);
  rcs.proj_view =
      get_projection_view_matrix(info.base.camera, info.base.viewport);

  return rcs;
}

void bind_render_pass_resources(const RgRuntime &rg, RenderPass &render_pass,
                                const DepthOnlyRenderPassResources &rcs) {
  render_pass.set_push_constants(glsl::EarlyZPassArgs{
      .meshes = rg.get_buffer_device_ptr(rcs.meshes),
      .mesh_instances = rg.get_buffer_device_ptr(rcs.mesh_instances),
      .transform_matrices = rg.get_buffer_device_ptr(rcs.transform_matrices),
      .proj_view = rcs.proj_view,
  });
}

template <> struct RenderPassResources<DrawSet::Opaque> {
  RgBufferToken<glsl::Mesh> meshes;
  RgBufferToken<glsl::MeshInstance> mesh_instances;
  RgBufferToken<glm::mat4x3> transform_matrices;
  RgBufferToken<glm::mat3> normal_matrices;
  RgBufferToken<glsl::Material> materials;
  RgBufferToken<glsl::DirectionalLight> directional_lights;
  RgTextureToken exposure;
  glm::mat4 proj_view;
  glm::vec3 eye;
  u32 num_directional_lights = 0;
};

using OpaqueRenderPassResources = RenderPassResources<DrawSet::Opaque>;

auto get_render_pass_resources(const SceneData &scene,
                               const OpaqueMeshPassInfo &info,
                               RgPassBuilder &pass)
    -> OpaqueRenderPassResources {
  OpaqueRenderPassResources rcs;

  const RgGpuScene &gpu_scene = *info.base.rg_gpu_scene;

  rcs.meshes = pass.read_buffer(gpu_scene.meshes, VS_READ_BUFFER);
  rcs.mesh_instances =
      pass.read_buffer(gpu_scene.mesh_instances, VS_READ_BUFFER);
  rcs.transform_matrices =
      pass.read_buffer(gpu_scene.transform_matrices, VS_READ_BUFFER);
  rcs.normal_matrices =
      pass.read_buffer(gpu_scene.normal_matrices, VS_READ_BUFFER);
  rcs.materials = pass.read_buffer(gpu_scene.materials, FS_READ_BUFFER);
  rcs.directional_lights =
      pass.read_buffer(gpu_scene.directional_lights, FS_READ_BUFFER);
  rcs.exposure = pass.read_texture(info.exposure, FS_READ_TEXTURE,
                                   info.exposure_temporal_layer);

  rcs.proj_view =
      get_projection_view_matrix(info.base.camera, info.base.viewport);
  rcs.eye = info.base.camera.position;
  rcs.num_directional_lights = scene.directional_lights.size();

  return rcs;
};

void bind_render_pass_resources(const RgRuntime &rg, RenderPass &render_pass,
                                const OpaqueRenderPassResources &rcs) {
  render_pass.bind_descriptor_sets({rg.get_texture_set()});
  auto [uniforms_host_ptr, uniforms_device_ptr, _] =
      rg.allocate<glsl::OpaquePassUniforms>();
  *uniforms_host_ptr = {
      .meshes = rg.get_buffer_device_ptr(rcs.meshes),
      .mesh_instances = rg.get_buffer_device_ptr(rcs.mesh_instances),
      .transform_matrices = rg.get_buffer_device_ptr(rcs.transform_matrices),
      .normal_matrices = rg.get_buffer_device_ptr(rcs.normal_matrices),
      .proj_view = rcs.proj_view,
  };
  render_pass.set_push_constants(glsl::OpaquePassArgs{
      .ub = uniforms_device_ptr,
      .materials = rg.get_buffer_device_ptr(rcs.materials),
      .directional_lights = rg.get_buffer_device_ptr(rcs.directional_lights),
      .num_directional_lights = rcs.num_directional_lights,
      .eye = rcs.eye,
      .exposure = glsl::StorageTexture2D(
          rg.get_storage_texture_descriptor(rcs.exposure)),
  });
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

  for (BatchId b : range(ds.batches.size())) {
    {
      auto pass = ccfg.rgb->create_pass({fmt::format(
          "{}{}-prepare-batch-{}", info.base.pass_name, pass_type, b)});

      struct {
        Handle<ComputePipeline> pipeline;
        RgBufferToken<glsl::DispatchIndirectCommand> batch_prepare_commands;
        RgBufferToken<u32> batch_offsets;
        RgBufferToken<u32> batch_sizes;
        RgBufferToken<glsl::MeshletDrawCommand> command_descs;
        RgBufferToken<glsl::DrawIndexedIndirectCommand> commands;
        BatchId batch;
      } rcs;

      rcs.pipeline = ccfg.pipelines->prepare_batch;
      rcs.batch_prepare_commands = pass.read_buffer(
          cfg.batch_prepare_commands, INDIRECT_COMMAND_SRC_BUFFER);
      rcs.batch_offsets = pass.read_buffer(cfg.batch_offsets, CS_READ_BUFFER);
      rcs.batch_sizes = pass.read_buffer(cfg.batch_sizes, CS_READ_BUFFER);
      rcs.command_descs = pass.read_buffer(cfg.batch_commands, CS_READ_BUFFER);
      std::tie(commands, rcs.commands) =
          pass.write_buffer(fmt::format("{}{}-batch-{}-commands",
                                        info.base.pass_name, pass_type, b),
                            commands, CS_WRITE_BUFFER);
      rcs.batch = b;

      pass.set_compute_callback([rcs](Renderer &, const RgRuntime &rg,
                                      ComputePass &pass) {
        pass.bind_compute_pipeline(rcs.pipeline);
        pass.set_push_constants(glsl::PrepareBatchArgs{
            .batch_offset =
                rg.get_buffer_device_ptr(rcs.batch_offsets) + rcs.batch,
            .batch_size = rg.get_buffer_device_ptr(rcs.batch_sizes) + rcs.batch,
            .command_descs = rg.get_buffer_device_ptr(rcs.command_descs),
            .commands = rg.get_buffer_device_ptr(rcs.commands),
        });
        pass.dispatch_indirect(
            rg.get_buffer(rcs.batch_prepare_commands).slice(rcs.batch, 1));
      });
    }

    auto pass = ccfg.rgb->create_pass(
        {fmt::format("{}{}-batch-{}", info.base.pass_name, pass_type, b)});

    for (usize i = 0; i < info.base.color_attachments.size(); ++i) {
      NotNull<RgTextureId *> color_attachment = info.base.color_attachments[i];
      if (!*color_attachment) {
        continue;
      }
      ColorAttachmentOperations ops = info.base.color_attachment_ops[i];
      if (info.base.occlusion_culling_mode ==
              OcclusionCullingMode::SecondPhase or
          b > 0) {
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
            b > 0) {
          ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        std::tie(*info.base.depth_attachment, std::ignore) =
            pass.write_depth_attachment(info.base.depth_attachment_name,
                                        *info.base.depth_attachment, ops);
      }
    }

    struct {
      BatchId batch_id;
      BatchDesc batch;
      RgBufferToken<glsl::DrawIndexedIndirectCommand> commands;
      RgBufferToken<u32> batch_sizes;
      RenderPassResources<S> ext;
    } rcs;

    rcs.batch_id = b;
    rcs.batch = info.base.gpu_scene->draw_sets[draw_set].batches[b].desc;
    rcs.commands = pass.read_buffer(commands, INDIRECT_COMMAND_SRC_BUFFER);
    rcs.batch_sizes =
        pass.read_buffer(cfg.batch_sizes, INDIRECT_COMMAND_SRC_BUFFER);
    rcs.ext = get_render_pass_resources(*ccfg.scene, info, pass);

    pass.set_graphics_callback([rcs](Renderer &, const RgRuntime &rg,
                                     RenderPass &render_pass) {
      render_pass.bind_graphics_pipeline(rcs.batch.pipeline);
      render_pass.bind_index_buffer(rcs.batch.index_buffer,
                                    VK_INDEX_TYPE_UINT8_EXT);
      bind_render_pass_resources(rg, render_pass, rcs.ext);
      render_pass.draw_indexed_indirect_count(
          BufferView(rg.get_buffer(rcs.commands)),
          BufferView(rg.get_buffer(rcs.batch_sizes).slice(rcs.batch_id, 1)));
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
