#include "MeshPass.hpp"
#include "Batch.hpp"
#include "CommandRecorder.hpp"
#include "Profiler.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.h"
#include "glsl/MeshletCullingPass.h"
#include "glsl/OpaquePass.h"

#include <fmt/format.h>

namespace ren {

namespace {

struct CullingInfo {
  u32 draw_set = -1;
  NotNull<RgBufferId<glsl::DrawIndexedIndirectCommand> *> batch_commands;
  NotNull<RgBufferId<u32> *> batch_sizes;
};

void record_culling(const PassCommonConfig &ccfg, const MeshPassBaseInfo &info,
                    RgBuilder &rgb, const CullingInfo &cfg) {
  ren_prof_zone("Record culling");

  const DrawSetData &ds = info.gpu_scene->draw_sets[cfg.draw_set];
  const RgDrawSetData &rg_ds = info.rg_gpu_scene->draw_sets[cfg.draw_set];

  u32 num_instances = ds.mesh_instances.size();

  u32 num_meshlets = 0;
  auto batch_command_offsets = ccfg.allocator->allocate<u32>(ds.batches.size());
  for (auto i : range(ds.batches.size())) {
    batch_command_offsets.host_ptr[i] = num_meshlets;
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

  *cfg.batch_commands = rgb.create_buffer<glsl::DrawIndexedIndirectCommand>({
      .heap = BufferHeap::Static,
      .count = num_meshlets,
  });

  *cfg.batch_sizes = rgb.create_buffer<u32>({
      .heap = BufferHeap::Static,
      .count = ds.batches.size(),
  });

  {
    auto pass = rgb.create_pass(
        {.name = fmt::format("{}-init-culling", info.pass_name)});

    struct {
      RgUntypedBufferToken meshlet_bucket_commands;
      RgUntypedBufferToken meshlet_bucket_sizes;
      RgUntypedBufferToken batch_command_counts;
    } rcs;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("init-meshlet-bucket-commands",
                          meshlet_bucket_commands, TRANSFER_DST_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("init-meshlet-bucket-sizes", meshlet_bucket_sizes,
                          TRANSFER_DST_BUFFER);

    std::tie(*cfg.batch_sizes, rcs.batch_command_counts) = pass.write_buffer(
        "init-batch-command-counts", *cfg.batch_sizes, TRANSFER_DST_BUFFER);

    pass.set_callback([rcs](Renderer &, const RgRuntime &rg,
                            CommandRecorder &cmd) {
      std::array<glsl::DispatchIndirectCommand,
                 glsl::NUM_MESHLET_CULLING_BUCKETS>
          commands;
      std::ranges::fill(commands,
                        glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1});
      cmd.update_buffer(rg.get_buffer(rcs.meshlet_bucket_commands), commands);

      cmd.fill_buffer(rg.get_buffer(rcs.meshlet_bucket_sizes), 0);

      cmd.fill_buffer(rg.get_buffer(rcs.batch_command_counts), 0);
    });
  }

  {
    RgDebugName pass_name;
    if (info.occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
      pass_name = fmt::format("{}-instance-culling-and-lod-first-phase",
                              info.pass_name);
    } else if (info.occlusion_culling_mode ==
               OcclusionCullingMode::SecondPhase) {
      pass_name = fmt::format("{}-instance-culling-and-lod-second-phase",
                              info.pass_name);
    } else {
      pass_name = fmt::format("{}-instance-culling-and-lod", info.pass_name);
    }

    auto pass = rgb.create_pass({.name = std::move(pass_name)});

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

  {
    auto pass = rgb.create_pass(
        {.name = fmt::format("{}-meshlet-culling", info.pass_name)});

    struct {
      Handle<ComputePipeline> pipeline;
      RgBufferToken<glsl::Mesh> meshes;
      RgBufferToken<glm::mat4x3> transform_matrices;
      RgBufferToken<glsl::DispatchIndirectCommand> meshlet_bucket_commands;
      RgBufferToken<u32> meshlet_bucket_sizes;
      RgBufferToken<glsl::MeshletCullData> meshlet_cull_data;
      RgBufferToken<glsl::DrawIndexedIndirectCommand> batch_commands;
      DevicePtr<u32> batch_command_offsets;
      RgBufferToken<u32> batch_sizes;
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

    rcs.meshlet_bucket_sizes =
        pass.read_buffer(meshlet_bucket_sizes, CS_READ_BUFFER);

    rcs.meshlet_cull_data = pass.read_buffer(meshlet_cull_data, CS_READ_BUFFER);

    std::tie(*cfg.batch_commands, rcs.batch_commands) = pass.write_buffer(
        "batch-commands", *cfg.batch_commands, CS_WRITE_BUFFER);

    rcs.batch_command_offsets = batch_command_offsets.device_ptr;

    std::tie(*cfg.batch_sizes, rcs.batch_sizes) = pass.write_buffer(
        "batch-sizes", *cfg.batch_sizes, CS_READ_BUFFER | CS_WRITE_BUFFER);

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
                .commands = rg.get_buffer_device_ptr(rcs.batch_commands),
                .command_offsets = rcs.batch_command_offsets,
                .command_counts = rg.get_buffer_device_ptr(rcs.batch_sizes),
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
  RgBufferId<glsl::DrawIndexedIndirectCommand> batch_commands;
  RgBufferId<u32> batch_sizes;
};

template <DrawSet S>
void record_render_pass(const PassCommonConfig &ccfg,
                        const MeshPassInfo<S> &info,
                        const RenderPassInfo &cfg) {
  ren_prof_zone("Record render pass");

  u32 draw_set = get_draw_set_index(S);

  const RgDrawSetData &ds = info.base.rg_gpu_scene->draw_sets[draw_set];

  RgDebugName pass_name;
  if (info.base.occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
    pass_name = fmt::format("{}-first-phase", info.base.pass_name);
  } else if (info.base.occlusion_culling_mode ==
             OcclusionCullingMode::SecondPhase) {
    pass_name = fmt::format("{}-second-phase", info.base.pass_name);
  } else {
    pass_name = info.base.pass_name;
  }

  auto pass = ccfg.rgb->create_pass({.name = std::move(pass_name)});

  for (usize i = 0; i < info.base.color_attachments.size(); ++i) {
    NotNull<RgTextureId *> color_attachment = info.base.color_attachments[i];
    if (!*color_attachment) {
      continue;
    }
    ColorAttachmentOperations ops = info.base.color_attachment_ops[i];
    if (info.base.occlusion_culling_mode == OcclusionCullingMode::SecondPhase) {
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
          OcclusionCullingMode::SecondPhase) {
        ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
      }
      std::tie(*info.base.depth_attachment, std::ignore) =
          pass.write_depth_attachment(info.base.depth_attachment_name,
                                      *info.base.depth_attachment, ops);
    }
  }

  struct {
    Span<const Batch> batches;
    RgBufferToken<glsl::DrawIndexedIndirectCommand> batch_commands;
    RgBufferToken<u32> batch_sizes;
    RenderPassResources<S> ext;
  } rcs;

  rcs.batches = info.base.gpu_scene->draw_sets[draw_set].batches;
  rcs.batch_commands =
      pass.read_buffer(cfg.batch_commands, INDIRECT_COMMAND_SRC_BUFFER);
  rcs.batch_sizes =
      pass.read_buffer(cfg.batch_sizes, INDIRECT_COMMAND_SRC_BUFFER);
  rcs.ext = get_render_pass_resources(*ccfg.scene, info, pass);

  pass.set_graphics_callback([rcs](Renderer &, const RgRuntime &rg,
                                   RenderPass &render_pass) {
    BufferSlice<glsl::DrawIndexedIndirectCommand> batch_commands =
        rg.get_buffer(rcs.batch_commands);
    BufferSlice<u32> batch_sizes = rg.get_buffer(rcs.batch_sizes);
    for (const Batch &batch : rcs.batches) {
      render_pass.bind_graphics_pipeline(batch.desc.pipeline);
      render_pass.bind_index_buffer(batch.desc.index_buffer,
                                    VK_INDEX_TYPE_UINT8_EXT);
      bind_render_pass_resources(rg, render_pass, rcs.ext);
      render_pass.draw_indexed_indirect_count(
          batch_commands.slice(0, batch.num_meshlets), batch_sizes.slice(0, 1));
      batch_commands = batch_commands.slice(batch.num_meshlets);
      batch_sizes = batch_sizes.slice(1);
    }
  });
}

} // namespace

template <DrawSet S>
void record_mesh_pass(const PassCommonConfig &ccfg,
                      const MeshPassInfo<S> &info) {
  ren_prof_zone("MeshPass::record");
#if REN_RG_DEBUG
  ren_prof_zone_text(info.base.pass_name);
#endif

  RgBufferId<glsl::DrawIndexedIndirectCommand> batch_commands;
  RgBufferId<u32> batch_sizes;
  record_culling(ccfg, info.base, *ccfg.rgb,
                 CullingInfo{
                     .draw_set = get_draw_set_index(S),
                     .batch_commands = &batch_commands,
                     .batch_sizes = &batch_sizes,
                 });

  record_render_pass(ccfg, info,
                     RenderPassInfo{
                         .batch_commands = batch_commands,
                         .batch_sizes = batch_sizes,
                     });
}

#define add_mesh_pass(S)                                                       \
  template void record_mesh_pass(const PassCommonConfig &ccfg,                 \
                                 const MeshPassInfo<S> &info)

add_mesh_pass(DrawSet::DepthOnly);
add_mesh_pass(DrawSet::Opaque);

#undef add_mesh_pass

} // namespace ren
