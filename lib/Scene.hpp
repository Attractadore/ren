#pragma once
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "DenseHandleMap.hpp"
#include "Mesh.hpp"
#include "Passes.hpp"
#include "PipelineLoading.hpp"
#include "PostProcessingOptions.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Texture.hpp"
#include "TextureIdAllocator.hpp"
#include "glsl/Lighting.h"
#include "glsl/Material.h"
#include "ren/ren.hpp"

struct ImGuiContext;

namespace ren {

class Scene final : public IScene {
public:
  Scene(Renderer &renderer, Swapchain &swapchain);

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

  auto create_material(const MaterialCreateInfo &)
      -> expected<MaterialId> override;

  auto create_mesh_instances(std::span<const MeshInstanceCreateInfo> descs,
                             std::span<MeshInstanceId>)
      -> expected<void> override;

  void destroy_mesh_instances(
      std::span<const MeshInstanceId> mesh_instances) override;

  void set_mesh_instance_transforms(
      std::span<const MeshInstanceId> mesh_instances,
      std::span<const glm::mat4x3> transforms) override;

  auto create_directional_light() -> expected<DirectionalLightId> override;

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

  [[nodiscard]] auto get_or_create_sampler(const SamplerDesc &sampler)
      -> Handle<Sampler>;

  [[nodiscard]] auto get_or_create_texture(ImageId image,
                                           const SamplerDesc &sampler)
      -> SampledTextureId;
  void update_rg_config();

private:
  Renderer *m_renderer = nullptr;
  Swapchain *m_swapchain = nullptr;

  ResourceUploader m_resource_uploader;
  CommandAllocator m_cmd_allocator;

  ExposureMode m_exposure = ExposureMode::Automatic;
  Handle<Camera> m_camera;
  HandleMap<Camera> m_cameras;

  PostProcessingOptions m_pp_opts;

  std::array<VertexPoolList, glsl::NUM_MESH_ATTRIBUTE_FLAGS>
      m_vertex_pool_lists;
  Vector<Mesh> m_meshes = {{}};

  HashMap<SamplerDesc, Handle<Sampler>> m_samplers;

  std::unique_ptr<TextureIdAllocator> m_texture_allocator;

  Vector<glsl::Material> m_materials = {{}};

  DenseHandleMap<MeshInstance> m_mesh_instances;

  Vector<Handle<Texture>> m_images = Vector<Handle<Texture>>(1);

  Handle<DescriptorPool> m_persistent_descriptor_pool;
  Handle<DescriptorSetLayout> m_persistent_descriptor_set_layout;
  VkDescriptorSet m_persistent_descriptor_set = nullptr;

  DenseHandleMap<glsl::DirLight> m_dir_lights;

  ResourceArena m_arena;
  ResourceArena m_frame_arena;

  std::unique_ptr<RenderGraph> m_render_graph;

  PassesConfig m_rg_config;

  float m_lod_triangle_pixels = 16.0f;
  i32 m_lod_bias = 0;

  bool m_rg_valid : 1 = false;
  bool m_instance_frustum_culling : 1 = true;
  bool m_lod_selection : 1 = true;
  bool m_early_z : 1 = true;

  Pipelines m_pipelines;

#if REN_IMGUI
  ImGuiContext *m_imgui_context = nullptr;
#endif
};

} // namespace ren
