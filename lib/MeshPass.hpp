#pragma once
#include "Batch.hpp"
#include "BumpAllocator.hpp"
#include "CommandRecorder.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "Renderer.hpp"
#include "Support/NotNull.hpp"
#include "TextureIdAllocator.hpp"
#include "glsl/InstanceCullingAndLODPass.hpp"
#include "glsl/Lighting.h"

namespace ren {

class MeshPassClass {
public:
  struct BeginInfo;

  template <typename Self>
  void execute(this Self &self, Renderer &renderer, CommandRecorder &cmd,
               const Self::BeginInfo &begin_info) {
    typename Self::Instance instance(self, renderer, begin_info);
    instance.execute(renderer, cmd);
  };

protected:
  class Instance;

  struct BatchDraw {
    u32 num_meshlets = 0;
    Vector<glsl::InstanceCullData> instances;
  };

  using Batches = HashMap<BatchDesc, Vector<BatchDraw>>;

  Batches m_batches;
};

struct MeshPassClass::BeginInfo {
  NotNull<const GenArray<Mesh> *> host_meshes;
  NotNull<const GenArray<Material> *> host_materials;
  NotNull<const GenArray<MeshInstance> *> host_mesh_instances;
  Span<const BufferView> index_pools;
  NotNull<const Pipelines *> pipelines;

  u32 draw_size = 0;
  u32 num_draw_meshlets = 0;

  BufferView meshes;
  BufferView materials;
  BufferView mesh_instances;
  BufferView transform_matrices;
  BufferView normal_matrices;
  BufferView commands;

  VkDescriptorSet textures = nullptr;

  NotNull<DeviceBumpAllocator *> device_allocator;
  NotNull<UploadBumpAllocator *> upload_allocator;

  InstanceCullingAndLODSettings instance_culling_and_lod_settings;
  u32 meshlet_culling_feature_mask = 0;

  TempSpan<const Optional<ColorAttachment>> color_attachments;
  Optional<DepthStencilAttachment> depth_stencil_attachment;

  glm::uvec2 viewport;
  glm::mat4 proj_view;
  glm::vec3 eye;
};

class MeshPassClass::Instance {
protected:
  friend class MeshPassClass;

  Instance(MeshPassClass &cls, Renderer &renderer, const BeginInfo &begin_info);

  template <typename Self>
  void execute(this Self &self, Renderer &renderer, CommandRecorder &cmd) {
    ren_assert(self.m_draw_size > 0);
    ren_assert(self.m_num_draw_meshlets > 0);

    Batches &batches = self.m_class->m_batches;

    for (auto &[_, draws] : batches) {
      draws.clear();
    }
    self.build_batches(batches);

    u32 num_draws = 0;
    for (const auto &[_, draws] : batches) {
      num_draws += draws.size();
    }

    self.init_command_count_buffer(cmd, num_draws);

    u32 draw_id = 0;
    for (const auto &[batch, draws] : batches) {
      for (const BatchDraw &draw : draws) {
        bool is_first_draw = draw_id == 0;

        if (not is_first_draw) {
          self.insert_cross_draw_barriers(cmd);
        }

        self.run_culling(cmd, draw, self.m_command_count_ptr + draw_id);

        self.run_render_pass(
            cmd, batch,
            self.m_commands.template slice<glsl::DrawIndexedIndirectCommand>(
                0, draw.num_meshlets),
            self.m_command_count.template slice<u32>(draw_id));

        if (is_first_draw) {
          self.patch_attachments();
        }

        draw_id += 1;
      }
    }
  };

  void init_command_count_buffer(CommandRecorder &cmd, u32 num_draws);

  auto get_cross_draw_indirect_buffer_barrier() -> VkMemoryBarrier2;

  auto get_cross_draw_color_attachment_barrier() -> Optional<VkMemoryBarrier2>;

  auto get_cross_draw_depth_attachment_barrier() -> Optional<VkMemoryBarrier2>;

  void insert_cross_draw_barriers(CommandRecorder &cmd);

  void run_culling(CommandRecorder &cmd, const BatchDraw &draw,
                   DevicePtr<u32> command_count_ptr);

  template <typename Self>
  void run_render_pass(this Self &self, CommandRecorder &cmd,
                       const BatchDesc &batch, const BufferView &commands,
                       const BufferView &command_count) {
    RenderPass render_pass = cmd.render_pass({
        .color_attachments = self.m_color_attachments,
        .depth_stencil_attachment = self.m_depth_stencil_attachment,
    });
    render_pass.set_viewports({{
        .width = float(self.m_viewport.x),
        .height = float(self.m_viewport.y),
        .maxDepth = 1.0f,
    }});
    render_pass.set_scissor_rects(
        {{.extent = {self.m_viewport.x, self.m_viewport.y}}});
    render_pass.bind_graphics_pipeline(batch.pipeline);
    render_pass.bind_index_buffer(batch.index_buffer_view,
                                  VK_INDEX_TYPE_UINT8_EXT);
    self.bind_render_pass_resources(render_pass);
    render_pass.draw_indexed_indirect_count(commands, command_count);
  }

  void patch_attachments();

protected:
  MeshPassClass *m_class = nullptr;

  const GenArray<Mesh> *m_host_meshes = nullptr;
  const GenArray<Material> *m_host_materials = nullptr;
  const GenArray<MeshInstance> *m_host_mesh_instances = nullptr;
  Span<const BufferView> m_index_pools;
  const Pipelines *m_pipelines = nullptr;

  BufferView m_meshes;
  BufferView m_materials;
  BufferView m_mesh_instances;
  BufferView m_transform_matrices;
  BufferView m_normal_matrices;
  BufferView m_commands;
  BufferView m_command_count;

  DevicePtr<glsl::Mesh> m_meshes_ptr;
  DevicePtr<glsl::Material> m_materials_ptr;
  DevicePtr<glsl::MeshInstance> m_mesh_instances_ptr;
  DevicePtr<glm::mat4x3> m_transform_matrices_ptr;
  DevicePtr<glm::mat3> m_normal_matrices_ptr;
  DevicePtr<glsl::DrawIndexedIndirectCommand> m_commands_ptr;
  DevicePtr<u32> m_command_count_ptr;

  VkDescriptorSet m_textures = nullptr;

  DeviceBumpAllocator *m_device_allocator = nullptr;
  UploadBumpAllocator *m_upload_allocator = nullptr;

  u32 m_draw_size = 0;
  u32 m_num_draw_meshlets = 0;

  InstanceCullingAndLODSettings m_instance_culling_and_lod_settings;
  u32 m_meshlet_culling_feature_mask = 0;

  StaticVector<Optional<ColorAttachment>, 8> m_color_attachments;
  Optional<DepthStencilAttachment> m_depth_stencil_attachment;

  glm::uvec2 m_viewport;
  glm::mat4 m_proj_view;
  glm::vec3 m_eye;
};

class DepthOnlyMeshPassClass : public MeshPassClass {
public:
  struct BeginInfo;

private:
  friend class MeshPassClass;
  class Instance;
};

struct DepthOnlyMeshPassClass::BeginInfo {
  MeshPassClass::BeginInfo base;
};

class DepthOnlyMeshPassClass::Instance : public MeshPassClass::Instance {
private:
  friend class MeshPassClass;
  Instance(DepthOnlyMeshPassClass &cls, Renderer &renderer,
           const BeginInfo &begin_info);

  void build_batches(Batches &batches);

  void bind_render_pass_resources(RenderPass &render_pass);
};

class OpaqueMeshPassClass : public MeshPassClass {
public:
  class BeginInfo;

private:
  friend class MeshPassClass;
  class Instance;
};

struct OpaqueMeshPassClass::BeginInfo {
  MeshPassClass::BeginInfo base;
  u32 num_directional_lights = 0;
  BufferView directional_lights;
  StorageTextureId exposure;
};

class OpaqueMeshPassClass::Instance : public MeshPassClass::Instance {
private:
  friend class MeshPassClass;
  Instance(OpaqueMeshPassClass &cls, Renderer &renderer,
           const BeginInfo &begin_info);

  void build_batches(Batches &batches);

  void bind_render_pass_resources(RenderPass &render_pass);

private:
  u32 m_num_directional_lights = 0;

  BufferView m_directional_lights;

  DevicePtr<glsl::DirLight> m_directional_lights_ptr;

  StorageTextureId m_exposure;
};

} // namespace ren
