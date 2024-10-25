#pragma once
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "DescriptorAllocator.hpp"
#include "GpuScene.hpp"
#include "Light.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "Passes/Pass.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Support/GenArray.hpp"
#include "Support/GenMap.hpp"
#include "Texture.hpp"
#include "ren/ren.hpp"

struct ImGuiContext;

namespace ren {

using Image = Handle<Texture>;

struct ScenePerFrameResources {
  Handle<Semaphore> acquire_semaphore;
  Handle<Semaphore> present_semaphore;
  UploadBumpAllocator upload_allocator;
  CommandAllocator cmd_allocator;
  DescriptorAllocatorScope descriptor_allocator;

public:
  void reset();
};

struct SceneExposureSettings {
  ExposureMode mode = {};
  float ec = 0.0f;
};

struct SceneGraphicsSettings {
  // Instance culling and LOD
  bool instance_frustum_culling = true;
  bool instance_occulusion_culling = true;
  bool lod_selection = true;
  float lod_triangle_pixels = 16.0f;
  i32 lod_bias = 0;

  // Meshlet culling
  bool meshlet_cone_culling = true;
  bool meshlet_frustum_culling = true;
  bool meshlet_occlusion_culling = true;

  // Opaque pass
  bool early_z = true;
};

struct SceneData {
  SceneGraphicsSettings settings;

  SceneExposureSettings exposure;

  Handle<Camera> camera;
  GenArray<Camera> cameras;

  IndexPoolList index_pools;
  GenArray<Mesh> meshes;
  Vector<Handle<Mesh>> update_meshes;
  Vector<glsl::Mesh> mesh_update_data;

  GenArray<MeshInstance> mesh_instances;
  GenMap<glm::mat4x3, Handle<MeshInstance>> mesh_instance_transforms;
  Vector<Handle<MeshInstance>> update_mesh_instances;
  Vector<glsl::MeshInstance> mesh_instance_update_data;

  GenArray<Material> materials;
  Vector<Handle<Material>> update_materials;
  Vector<glsl::Material> material_update_data;

  GenArray<DirectionalLight> directional_lights;
  Vector<Handle<DirectionalLight>> update_directional_lights;
  Vector<glsl::DirectionalLight> directional_light_update_data;

public:
  const Camera &get_camera() const {
    ren_assert(camera);
    return cameras[camera];
  }
};

struct Samplers {
  Handle<Sampler> dflt;
  Handle<Sampler> hi_z_gen;
  Handle<Sampler> hi_z;
};

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
  void allocate_per_frame_resources();

  auto get_camera(CameraId camera) -> Camera &;

  [[nodiscard]] auto get_or_create_sampler(
      const SamplerCreateInfo &&create_info) -> Handle<Sampler>;

  [[nodiscard]] auto get_or_create_texture(Handle<Image> image,
                                           const SamplerDesc &sampler_desc)
      -> glsl::SampledTexture2D;

  auto build_rg() -> RenderGraph;

private:
  Renderer *m_renderer = nullptr;
  Swapchain *m_swapchain = nullptr;

#if REN_IMGUI
  ImGuiContext *m_imgui_context = nullptr;
#endif

  ResourceArena m_arena;
  ResourceArena m_fif_arena;
  std::unique_ptr<DescriptorAllocator> m_descriptor_allocator;
  SmallVector<ScenePerFrameResources, 3> m_per_frame_resources;
  ScenePerFrameResources *m_frcs = nullptr;
  u64 m_graphics_time = 0;
  u32 m_num_frames_in_flight = 0;
  u32 m_new_num_frames_in_flight = 2;
  Handle<Semaphore> m_graphics_semaphore;

  Pipelines m_pipelines;

  DeviceBumpAllocator m_device_allocator;

  GenArray<Image> m_images;
  HashMap<SamplerCreateInfo, Handle<Sampler>> m_sampler_cache;
  Samplers m_samplers;

  ResourceUploader m_resource_uploader;

  PassPersistentConfig m_pass_cfg;
  PassPersistentResources m_pass_rcs;
  std::unique_ptr<RgPersistent> m_rgp;

  SceneData m_data;
  GpuScene m_gpu_scene;
};

} // namespace ren
