#pragma once
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "DenseHandleMap.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Support/Hash.hpp"
#include "TextureIdAllocator.hpp"
#include "glsl/Lighting.hpp"
#include "glsl/Material.hpp"
#include "ren/ren.hpp"

struct ImGuiContext;

namespace ren {

template <> struct Hash<SamplerDesc> {
  auto operator()(const SamplerDesc &sampler) const noexcept -> usize;
};

class SceneImpl {
  ResourceUploader m_resource_uploader;
  CommandAllocator m_cmd_allocator;

  Camera m_camera;
  PostProcessingOptions m_pp_opts;

  std::array<VertexPoolList, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      m_vertex_pool_lists;
  Vector<Mesh> m_meshes = {{}};
  Vector<u32> m_batch_offsets;
  Vector<u32> m_batch_max_counts;

  HashMap<SamplerDesc, AutoHandle<Sampler>> m_samplers;

  std::unique_ptr<TextureIdAllocator> m_texture_allocator;

  Vector<glsl::Material> m_materials = {{}};

public:
  unsigned m_viewport_width = 1280;
  unsigned m_viewport_height = 720;

private:
  DenseHandleMap<MeshInstance> m_mesh_instances;

  Vector<AutoHandle<Texture>> m_images = Vector<AutoHandle<Texture>>(1);

  AutoHandle<DescriptorPool> m_persistent_descriptor_pool;
  AutoHandle<DescriptorSetLayout> m_persistent_descriptor_set_layout;
  VkDescriptorSet m_persistent_descriptor_set = nullptr;

  DenseHandleMap<glsl::DirLight> m_dir_lights;

  ResourceArena m_arena;
  ResourceArena m_frame_arena;

  std::unique_ptr<RenderGraph> m_render_graph;
  bool m_early_z : 1 = false;

  Pipelines m_pipelines;

#if REN_IMGUI
  ImGuiContext *m_imgui_context = nullptr;
#endif

public:
  SceneImpl(SwapchainImpl &swapchain);

  auto create_mesh(const MeshDesc &desc) -> MeshId;

private:
  [[nodiscard]] auto get_or_create_sampler(const SamplerDesc &sampler)
      -> Handle<Sampler>;

  [[nodiscard]] auto get_or_create_texture(ImageId image,
                                           const SamplerDesc &sampler)
      -> SampledTextureId;

public:
  auto create_image(const ImageDesc &desc) -> ImageId;

  void create_materials(Span<const MaterialDesc> descs, MaterialId *out);

  void set_camera(const CameraDesc &desc) noexcept;

  void set_tone_mapping(const ToneMappingDesc &desc) noexcept;

  void create_mesh_instances(Span<const MeshInstanceDesc> descs,
                             Span<const glm::mat4x3> transforms,
                             MeshInstanceId *out);

  void
  destroy_mesh_instances(Span<const MeshInstanceId> mesh_instances) noexcept;

  void
  set_mesh_instance_transforms(Span<const MeshInstanceId> mesh_instances,
                               Span<const glm::mat4x3> transforms) noexcept;

  auto create_directional_light(const DirectionalLightDesc &desc)
      -> DirectionalLightId;

  void destroy_directional_light(DirectionalLightId light) noexcept;

  void update_directional_light(DirectionalLightId light,
                                const DirectionalLightDesc &desc) noexcept;

  void draw();

  void next_frame();

#if REN_IMGUI
  void set_imgui_context(ImGuiContext *context) noexcept;

  auto get_imgui_context() const noexcept -> ImGuiContext * {
    return m_imgui_context;
  }

  void draw_imgui();
#endif
};

inline auto get_scene(SceneId scene) -> SceneImpl * {
  return std::bit_cast<SceneImpl *>(scene);
}

inline auto get_scene_id(const SceneImpl *scene) -> SceneId {
  return std::bit_cast<SceneId>(scene);
}

} // namespace ren
