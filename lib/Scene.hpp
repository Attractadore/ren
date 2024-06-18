#pragma once
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Passes/Present.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Support/GenArray.hpp"
#include "Support/GenMap.hpp"
#include "Texture.hpp"
#include "TextureIdAllocator.hpp"
#include "glsl/Lighting.h"
#include "ren/ren.hpp"

struct ImGuiContext;

namespace ren {

struct SceneRgConfig {
  u32 batch_size = 0;
  u32 num_meshes = 0;
  u32 num_mesh_instances = 0;
  u32 num_materials = 0;
  u32 num_directional_lights = 0;
  glm::uvec2 viewport;
  ExposureMode exposure = {};
#if REN_IMGUI
  ImGuiContext *imgui_context = nullptr;
  u32 num_imgui_vertices = 0;
  u32 num_imgui_indices = 0;
#endif
  bool early_z : 1 = false;
};

using Image = Handle<Texture>;

class Scene final : public IScene {
public:
  Scene(Renderer &renderer, Swapchain &swapchain);

  auto get_exposure_mode() const -> ExposureMode;

  auto get_exposure_compensation() const -> float;

  auto get_camera() const -> const Camera &;

  auto get_camera_proj_view() const -> glm::mat4;

  auto get_viewport() const -> glm::uvec2;

  auto get_pipelines() const -> const Pipelines &;

  auto get_draw_size() const -> u32;

  auto get_num_draw_meshlets() const -> u32;

  auto get_meshes() const -> const GenArray<Mesh> &;

  auto get_index_pools() const -> Span<const IndexPool>;

  auto get_materials() const -> const GenArray<Material> &;

  auto get_mesh_instances() const -> const GenArray<MeshInstance> &;

  auto get_mesh_instance_transforms() const
      -> const GenMap<glm::mat4x3, Handle<MeshInstance>> &;

  auto get_directional_lights() const -> const GenArray<glsl::DirLight> &;

  bool is_frustum_culling_enabled() const;

  bool is_lod_selection_enabled() const;

  auto get_lod_triangle_pixel_count() const -> float;

  auto get_lod_bias() const -> i32;

  bool is_meshlet_cone_culling_enabled() const;

  bool is_meshlet_frustum_culling_enabled() const;

  auto get_meshlet_culling_feature_mask() const -> u32;

  bool is_early_z_enabled() const;

public:
  auto create_camera() -> expected<CameraId> override;

  void destroy_camera(CameraId camera) override;

  void set_camera(CameraId camera) override;

  void set_camera_perspective_projection(
      CameraId camera, const CameraPerspectiveProjectionDesc &desc) override;

  void set_camera_orthographic_projection(
      CameraId camera, const CameraOrthographicProjectionDesc &desc) override;

  void set_camera_transform(CameraId camera,
                            const CameraTransformDesc &desc) override;

  void set_camera_parameters(CameraId camera,
                             const CameraParameterDesc &desc) override;

  void set_exposure(const ExposureDesc &desc) override;

  auto create_mesh(const MeshCreateInfo &desc) -> expected<MeshId> override;

  auto create_image(const ImageCreateInfo &desc) -> expected<ImageId> override;

  auto
  create_material(const MaterialCreateInfo &) -> expected<MaterialId> override;

  auto
  create_mesh_instances(std::span<const MeshInstanceCreateInfo> descs,
                        std::span<MeshInstanceId>) -> expected<void> override;

  void destroy_mesh_instances(
      std::span<const MeshInstanceId> mesh_instances) override;

  void set_mesh_instance_transforms(
      std::span<const MeshInstanceId> mesh_instances,
      std::span<const glm::mat4x3> transforms) override;

  auto create_directional_light(const DirectionalLightDesc &desc)
      -> expected<DirectionalLightId> override;

  void destroy_directional_light(DirectionalLightId light) override;

  void set_directional_light(DirectionalLightId light,
                             const DirectionalLightDesc &desc) override;

  auto draw() -> expected<void> override;

  void next_frame();

#if REN_IMGUI
  void set_imgui_context(ImGuiContext *context) noexcept;

  auto get_imgui_context() const noexcept -> ImGuiContext * {
    return m_imgui_context;
  }

  void draw_imgui();
#endif

private:
  auto get_camera(CameraId camera) -> Camera &;

  [[nodiscard]] auto get_or_create_sampler(
      const SamplerCreateInfo &&create_info) -> Handle<Sampler>;

  [[nodiscard]] auto
  get_or_create_texture(Handle<Image> image,
                        const SamplerDesc &sampler_desc) -> SampledTextureId;

  void update_rg_config();

  void setup_rg(RgBuilder &rgb);

private:
  Renderer *m_renderer = nullptr;
  Swapchain *m_swapchain = nullptr;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_acquire_semaphores;
  std::array<Handle<Semaphore>, PIPELINE_DEPTH> m_present_semaphores;
  PresentPassOutput m_present_pass_resources;

  ResourceUploader m_resource_uploader;
  CommandAllocator m_cmd_allocator;

  Handle<Camera> m_camera;
  GenArray<Camera> m_cameras;

  IndexPoolList m_index_pools;
  GenArray<Mesh> m_meshes;

  HashMap<SamplerCreateInfo, Handle<Sampler>> m_samplers;

  std::unique_ptr<TextureIdAllocator> m_texture_allocator;

  GenArray<Material> m_materials;

  GenArray<MeshInstance> m_mesh_instances;
  GenMap<glm::mat4x3, Handle<MeshInstance>> m_mesh_instance_transforms;

  GenArray<Image> m_images;

  Handle<DescriptorPool> m_persistent_descriptor_pool;
  Handle<DescriptorSetLayout> m_persistent_descriptor_set_layout;
  VkDescriptorSet m_persistent_descriptor_set = nullptr;

  GenArray<glsl::DirLight> m_dir_lights;

  ResourceArena m_arena;
  ResourceArena m_frame_arena;

  SceneRgConfig m_rg_config;
  std::unique_ptr<RenderGraph> m_render_graph;

  ExposureMode m_exposure_mode = {};
  float m_exposure_compensation = 0.0f;

  u32 m_draw_size = 8 * 1024;
  u32 m_num_draw_meshlets = 1024 * 1024;

  float m_lod_triangle_pixels = 16.0f;
  i32 m_lod_bias = 0;

  bool m_rg_valid : 1 = false;
  bool m_instance_frustum_culling : 1 = true;
  bool m_lod_selection : 1 = true;
  bool m_meshlet_cone_culling : 1 = true;
  bool m_meshlet_frustum_culling : 1 = true;
  bool m_early_z : 1 = true;

  Pipelines m_pipelines;

#if REN_IMGUI
  ImGuiContext *m_imgui_context = nullptr;
#endif
};

} // namespace ren
