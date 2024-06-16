#include "MeshPass.hpp"
#include "Support/Views.hpp"
#include "glsl/EarlyZPass.h"
#include "glsl/MeshletCullingPass.h"
#include "glsl/OpaquePass.h"

namespace ren {

MeshPassClass::Instance::Instance(MeshPassClass &cls, Renderer &renderer,
                                  const BeginInfo &begin_info) {
  m_class = &cls;

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
  m_commands = begin_info.commands;
  ren_assert(m_commands.size >=
             m_num_draw_meshlets * sizeof(glsl::DrawIndexedIndirectCommand));

  m_meshes_ptr = renderer.get_buffer_device_ptr<glsl::Mesh>(m_meshes);
  m_materials_ptr =
      renderer.try_get_buffer_device_ptr<glsl::Material>(m_materials);
  m_mesh_instances_ptr =
      renderer.get_buffer_device_ptr<glsl::MeshInstance>(m_mesh_instances);
  m_transform_matrices_ptr =
      renderer.get_buffer_device_ptr<glm::mat4x3>(m_transform_matrices);
  m_normal_matrices_ptr =
      renderer.try_get_buffer_device_ptr<glm::mat3>(m_normal_matrices);
  m_commands_ptr =
      renderer.get_buffer_device_ptr<glsl::DrawIndexedIndirectCommand>(
          m_commands);

  m_textures = begin_info.textures;

  m_device_allocator = begin_info.device_allocator;
  m_upload_allocator = begin_info.upload_allocator;

  m_instance_culling_and_lod_settings =
      begin_info.instance_culling_and_lod_settings;
  m_meshlet_culling_feature_mask = begin_info.meshlet_culling_feature_mask;

  m_color_attachments = begin_info.color_attachments;
  m_depth_stencil_attachment = begin_info.depth_stencil_attachment;

  m_viewport = begin_info.viewport;
  m_proj_view = begin_info.proj_view;
  m_eye = begin_info.eye;
}

void MeshPassClass::Instance::Instance::init_command_count_buffer(
    CommandRecorder &cmd, u32 num_draws) {
  auto alloc = m_device_allocator->allocate<u32>(num_draws);
  m_command_count = alloc.view;
  m_command_count_ptr = alloc.ptr;
  cmd.fill_buffer(m_command_count, 0);
  VkMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
  };
  cmd.pipeline_barrier({barrier}, {});
}

auto MeshPassClass::Instance::Instance::get_cross_draw_indirect_buffer_barrier()
    -> VkMemoryBarrier2 {
  return {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
  };
}

auto MeshPassClass::Instance::Instance::
    get_cross_draw_color_attachment_barrier() -> Optional<VkMemoryBarrier2> {
  for (const Optional<ColorAttachment> &attachment : m_color_attachments) {
    if (attachment) {
      ren_assert(attachment->ops.store == VK_ATTACHMENT_STORE_OP_STORE);
      return VkMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      };
    }
  }
  return None;
}

auto MeshPassClass::Instance::Instance::
    get_cross_draw_depth_attachment_barrier() -> Optional<VkMemoryBarrier2> {
  if (m_depth_stencil_attachment) {
    ren_assert(m_depth_stencil_attachment->depth_ops);
    ren_assert(!m_depth_stencil_attachment->stencil_ops);
    const DepthAttachmentOperations &ops =
        *m_depth_stencil_attachment->depth_ops;
    if (ops.store == VK_ATTACHMENT_STORE_OP_STORE) {
      return VkMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
          .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
          .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      };
    }
    ren_assert(ops.store == VK_ATTACHMENT_STORE_OP_NONE);
  }
  return None;
}

void MeshPassClass::Instance::Instance::insert_cross_draw_barriers(
    CommandRecorder &cmd) {
  StaticVector<VkMemoryBarrier2, 3> barriers;
  barriers.push_back(get_cross_draw_indirect_buffer_barrier());
  if (auto barrier = get_cross_draw_color_attachment_barrier()) {
    barriers.push_back(*barrier);
  }
  if (auto barrier = get_cross_draw_depth_attachment_barrier()) {
    barriers.push_back(*barrier);
  }
  cmd.pipeline_barrier(barriers, {});
}

void MeshPassClass::Instance::Instance::run_culling(
    CommandRecorder &cmd, const BatchDraw &draw,
    DevicePtr<u32> command_count_ptr) {
  auto [cull_data, cull_data_ptr, _] =
      m_upload_allocator->allocate<glsl::InstanceCullData>(
          draw.instances.size());
  std::ranges::copy(draw.instances, cull_data);

  u32 num_instances = draw.instances.size();

  u32 buckets_size = 0;
  std::array<u32, glsl::NUM_MESHLET_CULLING_BUCKETS> bucket_offsets;
  for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
    bucket_offsets[bucket] = buckets_size;
    u32 bucket_stride = 1 << bucket;
    u32 bucket_size =
        std::min(num_instances, draw.num_meshlets / bucket_stride);
    buckets_size += bucket_size;
  }

  auto [meshlet_bucket_commands_ptr, meshlet_bucket_commands] =
      m_device_allocator->allocate<glsl::DispatchIndirectCommand>(
          glsl::NUM_MESHLET_CULLING_BUCKETS);
  auto [meshlet_bucket_sizes_ptr, meshlet_bucket_sizes] =
      m_device_allocator->allocate<u32>(glsl::NUM_MESHLET_CULLING_BUCKETS);
  auto meshlet_cull_data_ptr =
      m_device_allocator->allocate<glsl::MeshletCullData>(buckets_size).ptr;
  {
    std::array<glsl::DispatchIndirectCommand, glsl::NUM_MESHLET_CULLING_BUCKETS>
        commands;
    std::ranges::fill(commands,
                      glsl::DispatchIndirectCommand{.x = 0, .y = 1, .z = 1});
    cmd.update_buffer(meshlet_bucket_commands, commands);

    cmd.fill_buffer(meshlet_bucket_sizes, 0);

    cmd.pipeline_barrier(
        {{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        }},
        {});
  }

  {
    ComputePass pass = cmd.compute_pass();
    float num_viewport_triangles =
        m_viewport.x * m_viewport.y /
        m_instance_culling_and_lod_settings.lod_triangle_pixel_count;
    float lod_triangle_density = num_viewport_triangles / 4.0f;
    pass.bind_compute_pipeline(m_pipelines->instance_culling_and_lod);
    auto [uniforms, ub, _] =
        m_upload_allocator->allocate<glsl::InstanceCullingAndLODPassUniforms>(
            1);
    *uniforms = {
        .feature_mask = m_instance_culling_and_lod_settings.feature_mask,
        .num_instances = num_instances,
        .proj_view = m_proj_view,
        .lod_triangle_density = lod_triangle_density,
        .lod_bias = m_instance_culling_and_lod_settings.lod_bias,
        .meshlet_bucket_offsets = bucket_offsets,
    };
    ren_assert(m_meshes_ptr);
    ren_assert(m_transform_matrices_ptr);
    ren_assert(cull_data_ptr);
    ren_assert(meshlet_bucket_commands_ptr);
    ren_assert(meshlet_bucket_sizes_ptr);
    ren_assert(meshlet_cull_data_ptr);
    pass.set_push_constants(glsl::InstanceCullingAndLODPassArgs{
        .ub = ub,
        .meshes = m_meshes_ptr,
        .transform_matrices = m_transform_matrices_ptr,
        .cull_data = cull_data_ptr,
        .meshlet_bucket_commands = meshlet_bucket_commands_ptr,
        .meshlet_bucket_sizes = meshlet_bucket_sizes_ptr,
        .meshlet_cull_data = meshlet_cull_data_ptr,
    });
    pass.dispatch_threads(num_instances,
                          glsl::INSTANCE_CULLING_AND_LOD_THREADS);
  }

  cmd.pipeline_barrier(
      {{
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      }},
      {});

  {
    ComputePass pass = cmd.compute_pass();
    pass.bind_compute_pipeline(m_pipelines->meshlet_culling);
    for (u32 bucket : range(glsl::NUM_MESHLET_CULLING_BUCKETS)) {
      ren_assert(m_meshes_ptr);
      ren_assert(m_transform_matrices_ptr);
      ren_assert(meshlet_cull_data_ptr);
      ren_assert(m_commands_ptr);
      ren_assert(command_count_ptr);
      ren_assert(m_eye != glm::vec3(0.0f));
      pass.set_push_constants(glsl::MeshletCullingPassArgs{
          .meshes = m_meshes_ptr,
          .transform_matrices = m_transform_matrices_ptr,
          .bucket_cull_data = meshlet_cull_data_ptr + bucket_offsets[bucket],
          .bucket_size = meshlet_bucket_sizes_ptr + bucket,
          .commands = m_commands_ptr,
          .num_commands = command_count_ptr,
          .feature_mask = m_meshlet_culling_feature_mask,
          .bucket = bucket,
          .eye = m_eye,
          .proj_view = m_proj_view,
      });
      pass.dispatch_indirect(
          meshlet_bucket_commands.slice<glsl::DispatchIndirectCommand>(bucket));
    }
  }

  cmd.pipeline_barrier(
      {{
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
          .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
      }},
      {});
}

void MeshPassClass::Instance::Instance::patch_attachments() {
  for (Optional<ColorAttachment> &attachment : m_color_attachments) {
    if (attachment) {
      attachment->ops.load = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
  }
  if (m_depth_stencil_attachment) {
    ren_assert(m_depth_stencil_attachment->depth_ops);
    m_depth_stencil_attachment->depth_ops->load = VK_ATTACHMENT_LOAD_OP_LOAD;
  }
}

DepthOnlyMeshPassClass::Instance::Instance(DepthOnlyMeshPassClass &cls,
                                           Renderer &renderer,
                                           const BeginInfo &begin_info)
    : MeshPassClass::Instance::Instance(cls, renderer, begin_info.base) {}

void DepthOnlyMeshPassClass::Instance::Instance::build_batches(
    Batches &batches) {
  Handle<GraphicsPipeline> pipeline = m_pipelines->early_z_pass;

  for (auto i : range(m_host_mesh_instances.size())) {
    const MeshInstance &mesh_instance = m_host_mesh_instances[i];
    const Mesh &mesh = m_host_meshes[mesh_instance.mesh];
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
        .mesh_instance = u32(i),
    });
  }
}

void DepthOnlyMeshPassClass::Instance::Instance::bind_render_pass_resources(
    RenderPass &render_pass) {
  ren_assert(m_meshes_ptr);
  ren_assert(m_mesh_instances_ptr);
  ren_assert(m_transform_matrices_ptr);
  render_pass.set_push_constants(glsl::EarlyZPassArgs{
      .meshes = m_meshes_ptr,
      .mesh_instances = m_mesh_instances_ptr,
      .transform_matrices = m_transform_matrices_ptr,
      .proj_view = m_proj_view,
  });
}

OpaqueMeshPassClass::Instance::Instance(OpaqueMeshPassClass &cls,
                                        Renderer &renderer,
                                        const BeginInfo &begin_info)
    : MeshPassClass::Instance::Instance(cls, renderer, begin_info.base) {
  m_num_directional_lights = begin_info.num_directional_lights;

  m_directional_lights = begin_info.directional_lights;

  m_directional_lights_ptr =
      renderer.get_buffer_device_ptr<glsl::DirLight>(m_directional_lights);

  m_exposure = begin_info.exposure;
}

void OpaqueMeshPassClass::Instance::build_batches(Batches &batches) {
  for (auto i : range(m_host_mesh_instances.size())) {
    const MeshInstance &mesh_instance = m_host_mesh_instances[i];

    const Mesh &mesh = m_host_meshes[mesh_instance.mesh];
    const Material &material = m_host_materials[mesh_instance.material];

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
        .mesh_instance = u32(i),
    });
  }
}

void OpaqueMeshPassClass::Instance::bind_render_pass_resources(
    RenderPass &render_pass) {
  ren_assert(m_textures);
  render_pass.bind_descriptor_sets({m_textures});
  auto [uniforms_host_ptr, uniforms_device_ptr, _] =
      m_upload_allocator->allocate<glsl::OpaquePassUniforms>(1);
  ren_assert(m_meshes_ptr);
  ren_assert(m_mesh_instances_ptr);
  ren_assert(m_transform_matrices_ptr);
  ren_assert(m_normal_matrices_ptr);
  *uniforms_host_ptr = {
      .meshes = m_meshes_ptr,
      .mesh_instances = m_mesh_instances_ptr,
      .transform_matrices = m_transform_matrices_ptr,
      .normal_matrices = m_normal_matrices_ptr,
      .proj_view = m_proj_view,
  };
  ren_assert(m_materials_ptr);
  ren_assert(m_directional_lights_ptr);
  ren_assert(m_exposure);
  render_pass.set_push_constants(glsl::OpaquePassArgs{
      .ub = uniforms_device_ptr,
      .materials = m_materials_ptr,
      .directional_lights = m_directional_lights_ptr,
      .num_directional_lights = m_num_directional_lights,
      .eye = m_eye,
      .exposure_texture = m_exposure,
  });
}

} // namespace ren
