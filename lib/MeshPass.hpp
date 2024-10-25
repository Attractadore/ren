#pragma once
#include "Batch.hpp"
#include "BumpAllocator.hpp"
#include "Camera.hpp"
#include "CommandRecorder.hpp"
#include "GpuScene.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "Profiler.hpp"
#include "RenderGraph.hpp"
#include "Renderer.hpp"
#include "Support/NotNull.hpp"
#include "glsl/Culling.h"
#include "glsl/Indirect.h"
#include "glsl/InstanceCullingAndLODPass.h"

#include <fmt/format.h>

namespace ren {

struct SceneData;
struct Samplers;

namespace glsl {
struct Mesh;
struct MeshInstance;
struct Material;
struct DirectionalLight;
} // namespace glsl

enum class OcclusionCullingMode {
  Disabled = glsl::INSTANCE_CULLING_AND_LOD_NO_OCCLUSION_CULLING,
  FirstPhase = glsl::INSTANCE_CULLING_AND_LOD_FIRST_PHASE,
  SecondPhase = glsl::INSTANCE_CULLING_AND_LOD_SECOND_PHASE,
  ThirdPhase = glsl::INSTANCE_CULLING_AND_LOD_THIRD_PHASE,
};

class MeshPassClass {
public:
  struct BeginInfo;

  template <typename Self>
  void record(this Self &self, RgBuilder &rgb,
              const Self::BeginInfo &begin_info) {
    typename Self::Instance instance(self, begin_info);
    instance.record(rgb);
  };

protected:
  class Instance;

  struct Batch {
    BatchDesc desc;
    u32 num_meshlets = 0;
  };

  Vector<Batch> m_batches;

  String m_pass_name;
  StaticVector<String, 8> m_color_attachment_names;
  String m_depth_attachment_name;
};

struct MeshPassClass::BeginInfo {
  StringView pass_name;

  TempSpan<const NotNull<RgTextureId *>> color_attachments;
  TempSpan<const ColorAttachmentOperations> color_attachment_ops;
  TempSpan<const StringView> color_attachment_names;

  NotNull<RgTextureId *> depth_attachment;
  DepthAttachmentOperations depth_attachment_ops;
  StringView depth_attachment_name;

  NotNull<const Pipelines *> pipelines;
  NotNull<const Samplers *> samplers;

  NotNull<const SceneData *> scene;
  Camera camera;
  glm::uvec2 viewport = {};

  NotNull<RgGpuScene *> gpu_scene;

  OcclusionCullingMode occlusion_culling_mode = OcclusionCullingMode::Disabled;
  RgTextureId hi_z;

  NotNull<UploadBumpAllocator *> upload_allocator;
};

class MeshPassClass::Instance {
protected:
  friend class MeshPassClass;

  Instance(MeshPassClass &cls, const BeginInfo &begin_info);

  template <typename Self> void record(this Self &self, RgBuilder &rgb) {
    ren_prof_zone("MeshPass::record");
    ren_prof_zone_text(self.m_class->m_pass_name);

    self.m_instance_cull_data =
        self.m_upload_allocator->template allocate<glsl::InstanceCullData>(
            self.m_scene->mesh_instances.size());

    self.build_batches();

    RgBufferId<glsl::DrawIndexedIndirectCommand> commands;
    RgBufferId<u32> command_counts;
    self.record_culling(rgb, CullingConfig{
                                 .commands = &commands,
                                 .command_counts = &command_counts,
                             });

    self.record_render_pass(rgb, commands, command_counts);
  }

  template <typename Self> void build_batches(this Self &self) {
    ren_prof_zone("Build batches");
    auto &batches = self.m_class->m_batches;
    batches.clear();
    usize i = 0;
    for (const auto &[h, mesh_instance] : self.m_scene->mesh_instances) {
      BatchDesc batch_desc = self.get_batch_desc(mesh_instance);

      auto it = std::ranges::find_if(batches, [&](const Batch &batch) {
        return batch.desc == batch_desc;
      });

      Batch *batch;
      [[unlikely]] if (it == batches.end()) {
        batch = &batches.emplace_back(Batch{batch_desc});
      } else {
        batch = &*it;
      }

      const Mesh &mesh = self.m_scene->meshes.get(mesh_instance.mesh);
      u32 num_meshlets = mesh.lods[0].num_meshlets;
      batch->num_meshlets += num_meshlets;

      self.m_instance_cull_data.host_ptr[i++] = {
          .mesh = mesh_instance.mesh,
          .mesh_instance = h,
          .batch = u32(batch - batches.data()),
      };
    }
  }

  struct CullingConfig {
    NotNull<RgBufferId<glsl::DrawIndexedIndirectCommand> *> commands;
    NotNull<RgBufferId<u32> *> command_counts;
  };

  void record_culling(RgBuilder &rgb, const CullingConfig &cfg);

  template <typename Self>
  void record_render_pass(this Self &self, RgBuilder &rgb,
                          RgBufferId<glsl::DrawIndexedIndirectCommand> commands,
                          RgBufferId<u32> command_counts) {
    ren_prof_zone("Record render pass");

    RgDebugName pass_name;
    if (self.m_occlusion_culling_mode == OcclusionCullingMode::FirstPhase) {
      pass_name = fmt::format("{}-first-phase", self.m_class->m_pass_name);
    } else if (self.m_occlusion_culling_mode ==
               OcclusionCullingMode::SecondPhase) {
      pass_name = fmt::format("{}-second-phase", self.m_class->m_pass_name);
    } else {
      pass_name = self.m_class->m_pass_name;
    }

    auto pass = rgb.create_pass({.name = std::move(pass_name)});

    for (usize i = 0; i < self.m_color_attachments.size(); ++i) {
      NotNull<RgTextureId *> color_attachment = self.m_color_attachments[i];
      if (!*color_attachment) {
        continue;
      }
      std::tie(*color_attachment, std::ignore) = pass.write_color_attachment(
          self.m_class->m_color_attachment_names[i], *color_attachment,
          self.m_color_attachment_ops[i]);
    }

    if (*self.m_depth_attachment) {
      if (self.m_depth_attachment_ops.store == VK_ATTACHMENT_STORE_OP_NONE) {
        pass.read_depth_attachment(*self.m_depth_attachment);
      } else {
        std::tie(*self.m_depth_attachment, std::ignore) =
            pass.write_depth_attachment(self.m_class->m_depth_attachment_name,
                                        *self.m_depth_attachment,
                                        self.m_depth_attachment_ops);
      }
    }

    struct {
      Vector<Batch> batches;
      RgBufferToken<glsl::DrawIndexedIndirectCommand> commands;
      RgBufferToken<u32> command_counts;
      typename Self::RenderPassResources ext;
    } rcs;

    rcs.batches = std::move(self.m_class->m_batches);
    rcs.commands = pass.read_buffer(commands, INDIRECT_COMMAND_SRC_BUFFER);
    rcs.command_counts =
        pass.read_buffer(command_counts, INDIRECT_COMMAND_SRC_BUFFER);
    rcs.ext = self.get_render_pass_resources(pass);

    pass.set_graphics_callback([rcs](Renderer &, const RgRuntime &rg,
                                     RenderPass &render_pass) {
      BufferSlice<glsl::DrawIndexedIndirectCommand> commands =
          rg.get_buffer(rcs.commands);
      BufferSlice<u32> command_counts = rg.get_buffer(rcs.command_counts);
      for (const Batch &batch : rcs.batches) {
        render_pass.bind_graphics_pipeline(batch.desc.pipeline);
        render_pass.bind_index_buffer(batch.desc.index_buffer,
                                      VK_INDEX_TYPE_UINT8_EXT);
        Self::bind_render_pass_resources(rg, render_pass, rcs.ext);
        render_pass.draw_indexed_indirect_count(
            commands.slice(0, batch.num_meshlets), command_counts.slice(0, 1));
        commands = commands.slice(batch.num_meshlets);
        command_counts = command_counts.slice(1);
      }
    });
  }

protected:
  MeshPassClass *m_class = nullptr;

  const Pipelines *m_pipelines = nullptr;
  const Samplers *m_samplers = nullptr;

  const SceneData *m_scene = nullptr;
  Camera m_camera;
  glm::uvec2 m_viewport = {};

  RgGpuScene *m_gpu_scene = nullptr;

  UploadBumpAllocator *m_upload_allocator = nullptr;

  StaticVector<NotNull<RgTextureId *>, 8> m_color_attachments;
  StaticVector<ColorAttachmentOperations, 8> m_color_attachment_ops;

  RgTextureId *m_depth_attachment = nullptr;
  DepthAttachmentOperations m_depth_attachment_ops;

  OcclusionCullingMode m_occlusion_culling_mode =
      OcclusionCullingMode::Disabled;
  RgTextureId m_hi_z;

  UploadBumpAllocation<glsl::InstanceCullData> m_instance_cull_data;
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
  Instance(DepthOnlyMeshPassClass &cls, const BeginInfo &begin_info);

  auto get_batch_desc(const MeshInstance &mesh_instance) -> BatchDesc;

  struct RenderPassResources {
    RgBufferToken<glsl::Mesh> meshes;
    RgBufferToken<glsl::MeshInstance> mesh_instances;
    RgBufferToken<glm::mat4x3> transform_matrices;
    glm::mat4 proj_view;
  };

  auto get_render_pass_resources(RgPassBuilder &pass) -> RenderPassResources;

  static void bind_render_pass_resources(const RgRuntime &rg,
                                         RenderPass &render_pass,
                                         const RenderPassResources &rcs);
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
  RgTextureId exposure;
  u32 exposure_temporal_layer = 0;
};

class OpaqueMeshPassClass::Instance : public MeshPassClass::Instance {
private:
  friend class MeshPassClass;
  Instance(OpaqueMeshPassClass &cls, const BeginInfo &begin_info);

  auto get_batch_desc(const MeshInstance &mesh_instance) -> BatchDesc;

  struct RenderPassResources {
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

  auto get_render_pass_resources(RgPassBuilder &pass) const
      -> RenderPassResources;

  static void bind_render_pass_resources(const RgRuntime &rg,
                                         RenderPass &render_pass,
                                         const RenderPassResources &rcs);

private:
  RgTextureId m_exposure;
  u32 m_exposure_temporal_layer = 0;
};

} // namespace ren
