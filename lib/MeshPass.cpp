#include "MeshPass.hpp"
#include "RenderGraph.hpp"
#include "Scene.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.h"
#include "glsl/InstanceCullingAndLODPass.h"
#include "glsl/MeshletCullingPass.h"
#include "glsl/OpaquePass.h"

#include <fmt/format.h>

namespace ren {

MeshPassClass::Instance::Instance(MeshPassClass &cls,
                                  const BeginInfo &begin_info) {
  m_class = &cls;

  m_class->m_pass_name = begin_info.pass_name;

  m_color_attachments = begin_info.color_attachments;
  m_color_attachment_ops = begin_info.color_attachment_ops;
  m_class->m_color_attachment_names = begin_info.color_attachment_names;

  m_depth_attachment = begin_info.depth_attachment;
  m_depth_attachment_ops = begin_info.depth_attachment_ops;
  m_class->m_depth_attachment_name = begin_info.depth_attachment_name;

  m_pipelines = begin_info.pipelines;
  m_samplers = begin_info.samplers;

  m_scene = begin_info.scene;
  m_camera = begin_info.camera;
  m_viewport = begin_info.viewport;

  m_gpu_scene = begin_info.gpu_scene;

  m_occlusion_culling_mode = begin_info.occlusion_culling_mode;
  m_hi_z = begin_info.hi_z;

  if (m_occlusion_culling_mode == OcclusionCullingMode::SecondPhase) {
    for (ColorAttachmentOperations &ops : m_color_attachment_ops) {
      ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    m_depth_attachment_ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
  }

  m_upload_allocator = begin_info.upload_allocator;
}

void MeshPassClass::Instance::Instance::record_culling(
    RgBuilder &rgb, const CullingConfig &cfg) {
  u32 num_instances = cfg.draw->instances.size();

  u32 buckets_size = 0;
  std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
  for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
    bucket_offsets[bucket] = buckets_size;
    u32 bucket_stride = 1 << bucket;
    u32 bucket_size =
        std::min(num_instances, cfg.draw->num_meshlets / bucket_stride);
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

  *cfg.commands = rgb.create_buffer<glsl::DrawIndexedIndirectCommand>({
      .heap = BufferHeap::Static,
      .size = usize(m_scene->settings.num_draw_meshlets),
  });

  *cfg.command_count = rgb.create_buffer<u32>({.heap = BufferHeap::Static});

  {
    auto pass = rgb.create_pass(
        {.name = fmt::format("{}-init-culling", m_class->m_pass_name)});

    struct {
      RgUntypedBufferToken meshlet_bucket_commands;
      RgUntypedBufferToken meshlet_bucket_sizes;
      RgUntypedBufferToken meshlet_draw_command_count;
    } rcs;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("init-meshlet-bucket-commands",
                          meshlet_bucket_commands, TRANSFER_DST_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("init-meshlet-bucket-sizes", meshlet_bucket_sizes,
                          TRANSFER_DST_BUFFER);

    std::tie(*cfg.command_count, rcs.meshlet_draw_command_count) =
        pass.write_buffer("init-meshlet-draw-command-count", *cfg.command_count,
                          TRANSFER_DST_BUFFER);

    pass.set_callback([rcs](Renderer &, const RgRuntime &rg,
                            CommandRecorder &cmd) {
      std::array<glsl::DispatchIndirectCommand,
                 glsl::NUM_MESHLET_CULLING_BUCKETS>
          commands;
      std::ranges::fill(commands,
                        glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1});
      cmd.update_buffer(rg.get_buffer(rcs.meshlet_bucket_commands), commands);

      cmd.fill_buffer(rg.get_buffer(rcs.meshlet_bucket_sizes), 0);

      cmd.fill_buffer(rg.get_buffer(rcs.meshlet_draw_command_count), 0);
    });
  }

  {
    RgDebugName pass_name;
    if (m_occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
      pass_name = fmt::format("{}-instance-culling-and-lod-first-phase",
                              m_class->m_pass_name);
    } else if (m_occlusion_culling_mode == OcclusionCullingMode::SecondPhase) {
      pass_name = fmt::format("{}-instance-culling-and-lod-second-phase",
                              m_class->m_pass_name);
    } else {
      pass_name =
          fmt::format("{}-instance-culling-and-lod", m_class->m_pass_name);
    }

    auto pass = rgb.create_pass({.name = std::move(pass_name)});

    struct {
      Handle<ComputePipeline> pipeline;
      DevicePtr<glsl::InstanceCullingAndLODPassUniforms> uniforms;
      RgBufferToken<glsl::Mesh> meshes;
      RgBufferToken<glm::mat4x3> transform_matrices;
      DevicePtr<glsl::InstanceCullData> instance_cull_data;
      u32 num_instances;
      RgBufferToken<glsl::DispatchIndirectCommand> meshlet_bucket_commands;
      RgBufferToken<u32> meshlet_bucket_sizes;
      RgBufferToken<glsl::MeshletCullData> meshlet_cull_data;
      RgBufferToken<MeshInstanceVisibilityMask> mesh_instance_visibility;
      RgTextureToken hi_z;
    } rcs;

    rcs.pipeline = m_pipelines->instance_culling_and_lod;

    rcs.meshes = pass.read_buffer(m_gpu_scene->meshes, CS_READ_BUFFER);

    rcs.transform_matrices =
        pass.read_buffer(m_gpu_scene->transform_matrices, CS_READ_BUFFER);

    auto [instance_cull_data, instance_cull_data_ptr, _] =
        m_upload_allocator->allocate<glsl::InstanceCullData>(
            cfg.draw->instances.size());
    std::ranges::copy(cfg.draw->instances, instance_cull_data);
    rcs.instance_cull_data = instance_cull_data_ptr;
    rcs.num_instances = num_instances;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("meshlet-bucket-commands", meshlet_bucket_commands,
                          CS_WRITE_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("meshlet-bucket-sizes", meshlet_bucket_sizes,
                          CS_WRITE_BUFFER);

    std::tie(meshlet_cull_data, rcs.meshlet_cull_data) = pass.write_buffer(
        "meshlet-cull-data", meshlet_cull_data, CS_WRITE_BUFFER);

    const SceneGraphicsSettings &settings = m_scene->settings;

    u32 feature_mask = 0;
    if (settings.instance_frustum_culling) {
      feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_FRUSTUM_BIT;
    }
    if (settings.lod_selection) {
      feature_mask |= glsl::INSTANCE_CULLING_AND_LOD_LOD_SELECTION_BIT;
    }
    feature_mask |= (u32)m_occlusion_culling_mode;

    float num_viewport_triangles =
        m_viewport.x * m_viewport.y / settings.lod_triangle_pixels;
    float lod_triangle_density = num_viewport_triangles / 4.0f;
    i32 lod_bias = settings.lod_bias;

    auto [uniforms, uniforms_ptr, _2] =
        m_upload_allocator->allocate<glsl::InstanceCullingAndLODPassUniforms>(
            1);
    *uniforms = {
        .feature_mask = feature_mask,
        .num_instances = num_instances,
        .proj_view = get_projection_view_matrix(m_camera, m_viewport),
        .lod_triangle_density = lod_triangle_density,
        .lod_bias = lod_bias,
        .meshlet_bucket_offsets = bucket_offsets,
    };
    rcs.uniforms = uniforms_ptr;

    if (m_occlusion_culling_mode == OcclusionCullingMode::SecondPhase) {
      ren_assert(m_hi_z);
      std::tie(m_gpu_scene->mesh_instance_visibility,
               rcs.mesh_instance_visibility) =
          pass.write_buffer("new-mesh-instance-visibility",
                            m_gpu_scene->mesh_instance_visibility,
                            CS_READ_WRITE_BUFFER);
      rcs.hi_z = pass.read_texture(m_hi_z, CS_SAMPLE_TEXTURE, m_samplers->hi_z);
    } else if (m_occlusion_culling_mode != OcclusionCullingMode::Disabled) {
      rcs.mesh_instance_visibility = pass.read_buffer(
          m_gpu_scene->mesh_instance_visibility, CS_READ_BUFFER);
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
          .cull_data = rcs.instance_cull_data,
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
        {.name = fmt::format("{}-meshlet-culling", m_class->m_pass_name)});

    struct {
      Handle<ComputePipeline> pipeline;
      RgBufferToken<glsl::Mesh> meshes;
      RgBufferToken<glm::mat4x3> transform_matrices;
      RgBufferToken<glsl::DispatchIndirectCommand> meshlet_bucket_commands;
      RgBufferToken<u32> meshlet_bucket_sizes;
      RgBufferToken<glsl::MeshletCullData> meshlet_cull_data;
      RgBufferToken<glsl::DrawIndexedIndirectCommand> meshlet_draw_commands;
      RgBufferToken<u32> meshlet_draw_command_count;
      DevicePtr<glm::mat4> proj_view;
      u32 feature_mask;
      std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
      glm::vec3 eye;
    } rcs;

    rcs.pipeline = m_pipelines->meshlet_culling;

    rcs.meshes = pass.read_buffer(m_gpu_scene->meshes, CS_READ_BUFFER);

    rcs.transform_matrices =
        pass.read_buffer(m_gpu_scene->transform_matrices, CS_READ_BUFFER);

    rcs.meshlet_bucket_commands =
        pass.read_buffer(meshlet_bucket_commands, INDIRECT_COMMAND_SRC_BUFFER);

    rcs.meshlet_bucket_sizes =
        pass.read_buffer(meshlet_bucket_sizes, CS_READ_BUFFER);

    rcs.meshlet_cull_data = pass.read_buffer(meshlet_cull_data, CS_READ_BUFFER);

    std::tie(*cfg.commands, rcs.meshlet_draw_commands) = pass.write_buffer(
        "meshlet-draw-commands", *cfg.commands, CS_WRITE_BUFFER);

    std::tie(*cfg.command_count, rcs.meshlet_draw_command_count) =
        pass.write_buffer("meshlet-draw-command-count", *cfg.command_count,
                          CS_READ_BUFFER | CS_WRITE_BUFFER);

    auto [proj_view, proj_view_ptr, _] =
        m_upload_allocator->allocate<glm::mat4>(1);
    *proj_view = get_projection_view_matrix(m_camera, m_viewport);
    rcs.proj_view = proj_view_ptr;

    const SceneGraphicsSettings &settings = m_scene->settings;

    rcs.feature_mask = 0;
    if (settings.meshlet_cone_culling) {
      rcs.feature_mask |= glsl::MESHLET_CULLING_CONE_BIT;
    }
    if (settings.meshlet_frustum_culling) {
      rcs.feature_mask |= glsl::MESHLET_CULLING_FRUSTUM_BIT;
    }

    rcs.bucket_offsets = bucket_offsets;
    rcs.eye = m_camera.position;

    pass.set_compute_callback(
        [rcs](Renderer &, const RgRuntime &rg, ComputePass &pass) {
          pass.bind_compute_pipeline(rcs.pipeline);
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
                .commands = rg.get_buffer_device_ptr(rcs.meshlet_draw_commands),
                .num_commands =
                    rg.get_buffer_device_ptr(rcs.meshlet_draw_command_count),
                .proj_view = rcs.proj_view,
                .feature_mask = rcs.feature_mask,
                .bucket = bucket,
                .eye = rcs.eye,
            });
            pass.dispatch_indirect(
                rg.get_buffer(rcs.meshlet_bucket_commands).slice(bucket));
          }
        });
  }
}

DepthOnlyMeshPassClass::Instance::Instance(DepthOnlyMeshPassClass &cls,
                                           const BeginInfo &begin_info)
    : MeshPassClass::Instance::Instance(cls, begin_info.base) {}

void DepthOnlyMeshPassClass::Instance::Instance::build_batches(
    Batches &batches) {
  Handle<GraphicsPipeline> pipeline = m_pipelines->early_z_pass;

  for (const auto &[h, mesh_instance] : m_scene->mesh_instances) {
    const Mesh &mesh = m_scene->meshes.get(mesh_instance.mesh);
    BatchDesc batch = {
        .pipeline = pipeline,
        .index_buffer = m_scene->index_pools[mesh.index_pool].indices,
    };
    auto it = batches.find(batch);
    [[unlikely]] if (it == batches.end()) {
      it = batches.insert(it, batch, {});
    }
    u32 num_meshlets = mesh.lods[0].num_meshlets;
    auto &batch_draws = it->second;
    [[unlikely]] if (batch_draws.empty()) { batch_draws.emplace_back(); }
    BatchDraw *draw = &batch_draws.back();
    [[unlikely]] if (draw->instances.size() == m_scene->settings.draw_size or
                     draw->num_meshlets + num_meshlets >
                         m_scene->settings.num_draw_meshlets) {
      draw = &batch_draws.emplace_back();
    }
    draw->num_meshlets += num_meshlets;
    draw->instances.push_back({
        .mesh = mesh_instance.mesh,
        .mesh_instance = h,
    });
  }
}

auto DepthOnlyMeshPassClass::Instance::get_render_pass_resources(
    RgPassBuilder &pass) -> RenderPassResources {
  RenderPassResources rcs;

  rcs.meshes = pass.read_buffer(m_gpu_scene->meshes, VS_READ_BUFFER);
  rcs.mesh_instances =
      pass.read_buffer(m_gpu_scene->mesh_instances, VS_READ_BUFFER);
  rcs.transform_matrices =
      pass.read_buffer(m_gpu_scene->transform_matrices, VS_READ_BUFFER);
  rcs.proj_view = get_projection_view_matrix(m_camera, m_viewport);

  return rcs;
}

void DepthOnlyMeshPassClass::Instance::bind_render_pass_resources(
    const RgRuntime &rg, RenderPass &render_pass,
    const RenderPassResources &rcs) {
  render_pass.set_push_constants(glsl::EarlyZPassArgs{
      .meshes = rg.get_buffer_device_ptr(rcs.meshes),
      .mesh_instances = rg.get_buffer_device_ptr(rcs.mesh_instances),
      .transform_matrices = rg.get_buffer_device_ptr(rcs.transform_matrices),
      .proj_view = rcs.proj_view,
  });
}

OpaqueMeshPassClass::Instance::Instance(OpaqueMeshPassClass &cls,
                                        const BeginInfo &begin_info)
    : MeshPassClass::Instance::Instance(cls, begin_info.base) {
  m_exposure = begin_info.exposure;
  m_exposure_temporal_layer = begin_info.exposure_temporal_layer;
}

void OpaqueMeshPassClass::Instance::build_batches(Batches &batches) {
  for (const auto &[h, mesh_instance] : m_scene->mesh_instances) {
    const Mesh &mesh = m_scene->meshes.get(mesh_instance.mesh);
    const Material &material = m_scene->materials.get(mesh_instance.material);

    MeshAttributeFlags attributes;
    if (material.base_color_texture) {
      attributes |= MeshAttribute::UV;
    }
    if (material.normal_texture) {
      attributes |= MeshAttribute::UV | MeshAttribute::Tangent;
    }
    if (mesh.colors) {
      attributes |= MeshAttribute::Color;
    }

    BatchDesc batch = {
        .pipeline = m_pipelines->opaque_pass[i32(attributes.get())],
        .index_buffer = m_scene->index_pools[mesh.index_pool].indices,
    };
    auto it = batches.find(batch);
    [[unlikely]] if (it == batches.end()) {
      it = batches.insert(it, batch, {});
    }
    u32 num_meshlets = mesh.lods[0].num_meshlets;
    auto &batch_draws = it->second;
    [[unlikely]] if (batch_draws.empty()) { batch_draws.emplace_back(); }
    BatchDraw *draw = &batch_draws.back();
    [[unlikely]] if (draw->instances.size() == m_scene->settings.draw_size or
                     draw->num_meshlets + num_meshlets >
                         m_scene->settings.num_draw_meshlets) {
      draw = &batch_draws.emplace_back();
    }
    draw->num_meshlets += num_meshlets;
    draw->instances.push_back({
        .mesh = mesh_instance.mesh,
        .mesh_instance = h,
    });
  }
}

auto OpaqueMeshPassClass::Instance::get_render_pass_resources(
    RgPassBuilder &pass) const -> RenderPassResources {
  RenderPassResources rcs;

  rcs.meshes = pass.read_buffer(m_gpu_scene->meshes, VS_READ_BUFFER);
  rcs.mesh_instances =
      pass.read_buffer(m_gpu_scene->mesh_instances, VS_READ_BUFFER);
  rcs.transform_matrices =
      pass.read_buffer(m_gpu_scene->transform_matrices, VS_READ_BUFFER);
  rcs.normal_matrices =
      pass.read_buffer(m_gpu_scene->normal_matrices, VS_READ_BUFFER);
  rcs.materials = pass.read_buffer(m_gpu_scene->materials, FS_READ_BUFFER);
  rcs.directional_lights =
      pass.read_buffer(m_gpu_scene->directional_lights, FS_READ_BUFFER);
  rcs.exposure =
      pass.read_texture(m_exposure, FS_READ_TEXTURE, m_exposure_temporal_layer);

  rcs.proj_view = get_projection_view_matrix(m_camera, m_viewport);
  rcs.eye = m_camera.position;
  rcs.num_directional_lights = m_scene->directional_lights.size();

  return rcs;
};

void OpaqueMeshPassClass::Instance::bind_render_pass_resources(
    const RgRuntime &rg, RenderPass &render_pass,
    const RenderPassResources &rcs) {
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

} // namespace ren
