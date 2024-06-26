#include "MeshPass.hpp"
#include "RenderGraph.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.h"
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

  m_host_meshes = begin_info.host_meshes;
  m_host_materials = begin_info.host_materials;
  m_host_mesh_instances = begin_info.host_mesh_instances;
  m_index_pools = begin_info.index_pools;
  m_pipelines = begin_info.pipelines;

  m_draw_size = begin_info.draw_size;
  m_num_draw_meshlets = begin_info.num_draw_meshlets;

  m_meshes = begin_info.meshes;
  m_materials = begin_info.materials;
  m_mesh_instances = begin_info.mesh_instances;
  m_transform_matrices = begin_info.transform_matrices;
  m_normal_matrices = begin_info.normal_matrices;

  m_upload_allocator = begin_info.upload_allocator;

  m_instance_culling_and_lod_settings =
      begin_info.instance_culling_and_lod_settings;
  m_meshlet_culling_feature_mask = begin_info.meshlet_culling_feature_mask;

  m_viewport = begin_info.viewport;
  m_proj_view = begin_info.proj_view;
  m_eye = begin_info.eye;
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

  RgBufferId meshlet_bucket_commands = rgb.create_buffer({
      .heap = BufferHeap::Static,
      .size = sizeof(
          glsl::DispatchIndirectCommand[glsl::NUM_MESHLET_CULLING_BUCKETS]),
  });

  RgBufferId meshlet_bucket_sizes = rgb.create_buffer({
      .heap = BufferHeap::Static,
      .size = sizeof(u32[glsl::NUM_MESHLET_CULLING_BUCKETS]),
  });

  RgBufferId meshlet_cull_data = rgb.create_buffer({
      .heap = BufferHeap::Static,
      .size = sizeof(glsl::MeshletCullData) * buckets_size,
  });

  *cfg.commands = rgb.create_buffer({
      .heap = BufferHeap::Static,
      .size = sizeof(glsl::DrawIndexedIndirectCommand) * m_num_draw_meshlets,
  });

  *cfg.command_count = rgb.create_buffer({
      .heap = BufferHeap::Static,
      .size = sizeof(u32),
  });

  {
    auto pass = rgb.create_pass(
        {.name = fmt::format("{}-init-culling", m_class->m_pass_name)});

    struct {
      RgBufferToken meshlet_bucket_commands;
      RgBufferToken meshlet_bucket_sizes;
      RgBufferToken meshlet_draw_command_count;
    } rcs;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("init-meshlet-bucket-commands",
                          meshlet_bucket_commands, RG_TRANSFER_DST_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("init-meshlet-bucket-sizes", meshlet_bucket_sizes,
                          RG_TRANSFER_DST_BUFFER);

    std::tie(*cfg.command_count, rcs.meshlet_draw_command_count) =
        pass.write_buffer("init-meshlet-draw-command-count", *cfg.command_count,
                          RG_TRANSFER_DST_BUFFER);

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
    auto pass =
        rgb.create_pass({.name = fmt::format("{}-instance-culling-and-lod",
                                             m_class->m_pass_name)});

    struct {
      Handle<ComputePipeline> pipeline;
      DevicePtr<glsl::InstanceCullingAndLODPassUniforms> uniforms;
      RgBufferToken meshes;
      RgBufferToken transform_matrices;
      DevicePtr<glsl::InstanceCullData> instance_cull_data;
      u32 num_instances;
      RgBufferToken meshlet_bucket_commands;
      RgBufferToken meshlet_bucket_sizes;
      RgBufferToken meshlet_cull_data;
    } rcs;

    rcs.pipeline = m_pipelines->instance_culling_and_lod;

    rcs.meshes = pass.read_buffer(m_meshes, RG_CS_READ_BUFFER);

    rcs.transform_matrices =
        pass.read_buffer(m_transform_matrices, RG_CS_READ_BUFFER);

    auto [instance_cull_data, instance_cull_data_ptr, _] =
        m_upload_allocator->allocate<glsl::InstanceCullData>(
            cfg.draw->instances.size());
    std::ranges::copy(cfg.draw->instances, instance_cull_data);
    rcs.instance_cull_data = instance_cull_data_ptr;
    rcs.num_instances = num_instances;

    std::tie(meshlet_bucket_commands, rcs.meshlet_bucket_commands) =
        pass.write_buffer("meshlet-bucket-commands", meshlet_bucket_commands,
                          RG_CS_WRITE_BUFFER);

    std::tie(meshlet_bucket_sizes, rcs.meshlet_bucket_sizes) =
        pass.write_buffer("meshlet-bucket-sizes", meshlet_bucket_sizes,
                          RG_CS_WRITE_BUFFER);

    std::tie(meshlet_cull_data, rcs.meshlet_cull_data) = pass.write_buffer(
        "meshlet-cull-data", meshlet_cull_data, RG_CS_WRITE_BUFFER);

    u32 feature_mask = m_instance_culling_and_lod_settings.feature_mask;
    float num_viewport_triangles =
        m_viewport.x * m_viewport.y /
        m_instance_culling_and_lod_settings.lod_triangle_pixel_count;
    float lod_triangle_density = num_viewport_triangles / 4.0f;
    i32 lod_bias = m_instance_culling_and_lod_settings.lod_bias;

    auto [uniforms, uniforms_ptr, _2] =
        m_upload_allocator->allocate<glsl::InstanceCullingAndLODPassUniforms>(
            1);
    *uniforms = {
        .feature_mask = feature_mask,
        .num_instances = num_instances,
        .proj_view = m_proj_view,
        .lod_triangle_density = lod_triangle_density,
        .lod_bias = lod_bias,
        .meshlet_bucket_offsets = bucket_offsets,
    };
    rcs.uniforms = uniforms_ptr;

    pass.set_compute_callback([rcs](Renderer &, const RgRuntime &rg,
                                    ComputePass &cmd) {
      cmd.bind_compute_pipeline(rcs.pipeline);
      ren_assert(rcs.uniforms);
      ren_assert(rcs.instance_cull_data);
      cmd.set_push_constants(glsl::InstanceCullingAndLODPassArgs{
          .ub = rcs.uniforms,
          .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
          .transform_matrices =
              rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
          .cull_data = rcs.instance_cull_data,
          .meshlet_bucket_commands =
              rg.get_buffer_device_ptr<glsl::DispatchIndirectCommand>(
                  rcs.meshlet_bucket_commands),
          .meshlet_bucket_sizes =
              rg.get_buffer_device_ptr<u32>(rcs.meshlet_bucket_sizes),
          .meshlet_cull_data = rg.get_buffer_device_ptr<glsl::MeshletCullData>(
              rcs.meshlet_cull_data),
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
      RgBufferToken meshes;
      RgBufferToken transform_matrices;
      RgBufferToken meshlet_bucket_commands;
      RgBufferToken meshlet_bucket_sizes;
      RgBufferToken meshlet_cull_data;
      RgBufferToken meshlet_draw_commands;
      RgBufferToken meshlet_draw_command_count;
      u32 feature_mask;
      std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
      glm::vec3 eye;
      glm::mat4 proj_view;
    } rcs;

    rcs.pipeline = m_pipelines->meshlet_culling;

    rcs.meshes = pass.read_buffer(m_meshes, RG_CS_READ_BUFFER);

    rcs.transform_matrices =
        pass.read_buffer(m_transform_matrices, RG_CS_READ_BUFFER);

    rcs.meshlet_bucket_commands =
        pass.read_buffer(meshlet_bucket_commands, RG_INDIRECT_COMMAND_BUFFER);

    rcs.meshlet_bucket_sizes =
        pass.read_buffer(meshlet_bucket_sizes, RG_CS_READ_BUFFER);

    rcs.meshlet_cull_data =
        pass.read_buffer(meshlet_cull_data, RG_CS_READ_BUFFER);

    std::tie(*cfg.commands, rcs.meshlet_draw_commands) = pass.write_buffer(
        "meshlet-draw-commands", *cfg.commands, RG_CS_WRITE_BUFFER);

    std::tie(*cfg.command_count, rcs.meshlet_draw_command_count) =
        pass.write_buffer("meshlet-draw-command-count", *cfg.command_count,
                          RG_CS_READ_BUFFER | RG_CS_WRITE_BUFFER);

    rcs.feature_mask = m_meshlet_culling_feature_mask;
    rcs.bucket_offsets = bucket_offsets;
    rcs.eye = m_eye;
    rcs.proj_view = m_proj_view;

    pass.set_compute_callback([rcs](Renderer &, const RgRuntime &rg,
                                    ComputePass &pass) {
      pass.bind_compute_pipeline(rcs.pipeline);
      for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
        pass.set_push_constants(glsl::MeshletCullingPassArgs{
            .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
            .transform_matrices =
                rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
            .bucket_cull_data = rg.get_buffer_device_ptr<glsl::MeshletCullData>(
                                    rcs.meshlet_cull_data) +
                                rcs.bucket_offsets[bucket],
            .bucket_size =
                rg.get_buffer_device_ptr<u32>(rcs.meshlet_bucket_sizes) +
                bucket,
            .commands =
                rg.get_buffer_device_ptr<glsl::DrawIndexedIndirectCommand>(
                    rcs.meshlet_draw_commands),
            .num_commands =
                rg.get_buffer_device_ptr<u32>(rcs.meshlet_draw_command_count),
            .feature_mask = rcs.feature_mask,
            .bucket = bucket,
            .eye = rcs.eye,
            .proj_view = rcs.proj_view,
        });
        pass.dispatch_indirect(
            rg.get_buffer(rcs.meshlet_bucket_commands)
                .slice<glsl::DispatchIndirectCommand>(bucket));
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

  for (const auto &[h, mesh_instance] : *m_host_mesh_instances) {
    const Mesh &mesh = m_host_meshes->get(mesh_instance.mesh);
    BatchDesc batch = {
        .pipeline = pipeline,
        .index_buffer_view = m_index_pools[mesh.index_pool],
    };
    auto it = batches.find(batch);
    [[unlikely]] if (it == batches.end()) {
      it = batches.insert(it, batch, {});
    }
    u32 num_meshlets = mesh.lods[0].num_meshlets;
    auto &batch_draws = it->second;
    [[unlikely]] if (batch_draws.empty()) { batch_draws.emplace_back(); }
    BatchDraw *draw = &batch_draws.back();
    [[unlikely]] if (draw->instances.size() == m_draw_size or
                     draw->num_meshlets + num_meshlets > m_num_draw_meshlets) {
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

  rcs.meshes = pass.read_buffer(m_meshes, RG_VS_READ_BUFFER);
  rcs.mesh_instances = pass.read_buffer(m_mesh_instances, RG_VS_READ_BUFFER);
  rcs.transform_matrices =
      pass.read_buffer(m_transform_matrices, RG_VS_READ_BUFFER);

  rcs.proj_view = m_proj_view;

  return rcs;
}

void DepthOnlyMeshPassClass::Instance::bind_render_pass_resources(
    const RgRuntime &rg, RenderPass &render_pass,
    const RenderPassResources &rcs) {
  render_pass.set_push_constants(glsl::EarlyZPassArgs{
      .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
      .mesh_instances =
          rg.get_buffer_device_ptr<glsl::MeshInstance>(rcs.mesh_instances),
      .transform_matrices =
          rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
      .proj_view = rcs.proj_view,
  });
}

OpaqueMeshPassClass::Instance::Instance(OpaqueMeshPassClass &cls,
                                        const BeginInfo &begin_info)
    : MeshPassClass::Instance::Instance(cls, begin_info.base) {
  m_directional_lights = begin_info.directional_lights;
  m_num_directional_lights = begin_info.num_directional_lights;
  m_exposure = begin_info.exposure;
  m_exposure_temporal_layer = begin_info.exposure_temporal_layer;
}

void OpaqueMeshPassClass::Instance::build_batches(Batches &batches) {
  for (const auto &[h, mesh_instance] : *m_host_mesh_instances) {
    const Mesh &mesh = m_host_meshes->get(mesh_instance.mesh);
    const Material &material = m_host_materials->get(mesh_instance.material);

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
        .index_buffer_view = m_index_pools[mesh.index_pool],
    };
    auto it = batches.find(batch);
    [[unlikely]] if (it == batches.end()) {
      it = batches.insert(it, batch, {});
    }
    u32 num_meshlets = mesh.lods[0].num_meshlets;
    auto &batch_draws = it->second;
    [[unlikely]] if (batch_draws.empty()) { batch_draws.emplace_back(); }
    BatchDraw *draw = &batch_draws.back();
    [[unlikely]] if (draw->instances.size() == m_draw_size or
                     draw->num_meshlets + num_meshlets > m_num_draw_meshlets) {
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

  rcs.meshes = pass.read_buffer(m_meshes, RG_VS_READ_BUFFER);
  rcs.mesh_instances = pass.read_buffer(m_mesh_instances, RG_VS_READ_BUFFER);
  rcs.transform_matrices =
      pass.read_buffer(m_transform_matrices, RG_VS_READ_BUFFER);
  rcs.normal_matrices = pass.read_buffer(m_normal_matrices, RG_VS_READ_BUFFER);
  rcs.materials = pass.read_buffer(m_materials, RG_FS_READ_BUFFER);
  rcs.directional_lights =
      pass.read_buffer(m_directional_lights, RG_FS_READ_BUFFER);
  rcs.exposure = pass.read_texture(m_exposure, RG_FS_READ_TEXTURE,
                                   m_exposure_temporal_layer);

  rcs.proj_view = m_proj_view;
  rcs.eye = m_eye;
  rcs.num_directional_lights = m_num_directional_lights;

  return rcs;
};

void OpaqueMeshPassClass::Instance::bind_render_pass_resources(
    const RgRuntime &rg, RenderPass &render_pass,
    const RenderPassResources &rcs) {
  render_pass.bind_descriptor_sets({rg.get_texture_set()});
  auto [uniforms_host_ptr, uniforms_device_ptr, _] =
      rg.allocate<glsl::OpaquePassUniforms>();
  *uniforms_host_ptr = {
      .meshes = rg.get_buffer_device_ptr<glsl::Mesh>(rcs.meshes),
      .mesh_instances =
          rg.get_buffer_device_ptr<glsl::MeshInstance>(rcs.mesh_instances),
      .transform_matrices =
          rg.get_buffer_device_ptr<glm::mat4x3>(rcs.transform_matrices),
      .normal_matrices =
          rg.get_buffer_device_ptr<glm::mat3>(rcs.normal_matrices),
      .proj_view = rcs.proj_view,
  };
  render_pass.set_push_constants(glsl::OpaquePassArgs{
      .ub = uniforms_device_ptr,
      .materials = rg.get_buffer_device_ptr<glsl::Material>(rcs.materials),
      .directional_lights =
          rg.get_buffer_device_ptr<glsl::DirLight>(rcs.directional_lights),
      .num_directional_lights = rcs.num_directional_lights,
      .eye = rcs.eye,
      .exposure_texture = rg.get_storage_texture_descriptor(rcs.exposure),
  });
}

} // namespace ren
