#pragma once
#include "Camera.hpp"
#include "DescriptorAllocator.hpp"
#include "GpuScene.hpp"
#include "Light.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Texture.hpp"
#include "core/GenArray.hpp"
#include "core/GenMap.hpp"
#include "passes/Pass.hpp"
#include "ren/ren.hpp"

struct ImGuiContext;

namespace ren {

using Image = Handle<Texture>;

constexpr usize NUM_FRAMES_IN_FLIGHT = 2;

struct ScenePerFrameResources {
  Handle<Semaphore> acquire_semaphore;
  Handle<Semaphore> present_semaphore;
  UploadBumpAllocator upload_allocator;
  Handle<CommandPool> gfx_cmd_pool;
  Handle<CommandPool> async_cmd_pool;
  DescriptorAllocatorScope descriptor_allocator;
  Handle<Semaphore> end_semaphore;
  u64 end_time = 0;

public:
  auto reset(Renderer &renderer) -> Result<void, Error>;
};

struct SceneExposureSettings {
  ExposureMode mode = {};
  float ec = 0.0f;
};

struct SceneGraphicsSettings {
  bool async_compute = true;
  bool present_from_compute = true;

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

  bool ssao = true;
  i32 ssao_num_samples = 4;
  float ssao_radius = 1.0f;
  float ssao_lod_bias = 0.0f;
  bool ssao_full_res = false;

  bool amd_anti_lag = true;
};

struct SceneData {
  SceneGraphicsSettings settings;

  SceneExposureSettings exposure;

  Handle<Camera> camera;
  GenArray<Camera> cameras;

  IndexPoolList index_pools;
  GenArray<Mesh> meshes;

  GenArray<MeshInstance> mesh_instances;
  GenMap<glm::mat4x3, Handle<MeshInstance>> mesh_instance_transforms;

  GenArray<Material> materials;

  GenArray<DirectionalLight> directional_lights;

  glsl::SampledTexture2D dhr_lut;

  glm::vec3 env_luminance = {};
  glsl::SampledTextureCube env_map;

public:
  const Camera &get_camera() const {
    ren_assert(camera);
    return cameras[camera];
  }
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

  auto create_mesh(std::span<const std::byte> blob)
      -> expected<MeshId> override;

  auto create_image(std::span<const std::byte> blob)
      -> expected<ImageId> override;

  auto create_texture(const void *blob, usize size)
      -> expected<Handle<Texture>>;

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

  auto create_directional_light(const DirectionalLightDesc &desc)
      -> expected<DirectionalLightId> override;

  void destroy_directional_light(DirectionalLightId light) override;

  void set_directional_light(DirectionalLightId light,
                             const DirectionalLightDesc &desc) override;

  void set_environment_color(const glm::vec3 &luminance) override {
    m_data.env_luminance = luminance;
  }

  auto set_environment_map(ImageId image) -> expected<void> override;

  auto delay_input() -> expected<void> override;

  bool is_amd_anti_lag_available();

  bool is_amd_anti_lag_enabled();

  auto draw() -> expected<void> override;

  auto next_frame() -> Result<void, Error>;

#if REN_IMGUI
  void set_imgui_context(ImGuiContext *context) noexcept;

  auto get_imgui_context() const noexcept -> ImGuiContext * {
    return m_imgui_context;
  }

  void draw_imgui();
#endif

private:
  auto allocate_per_frame_resources() -> Result<void, Error>;

  auto get_camera(CameraId camera) -> Camera &;

  [[nodiscard]] auto get_or_create_texture(Handle<Image> image,
                                           const SamplerDesc &sampler_desc)
      -> Result<glsl::SampledTexture2D, Error>;

  auto build_rg() -> Result<RenderGraph, Error>;

private:
  Renderer *m_renderer = nullptr;
  Swapchain *m_swapchain = nullptr;

#if REN_IMGUI
  ImGuiContext *m_imgui_context = nullptr;
#endif

  ResourceArena m_arena;
  DescriptorAllocator m_descriptor_allocator;
  StaticVector<ScenePerFrameResources, NUM_FRAMES_IN_FLIGHT>
      m_per_frame_resources;
  ScenePerFrameResources *m_frcs = nullptr;
  u64 m_frame_index = u64(-1);

  Pipelines m_pipelines;

  DeviceBumpAllocator m_gfx_allocator;
  DeviceBumpAllocator m_async_allocator;
  std::array<DeviceBumpAllocator, 2> m_shared_allocators;

  GenArray<Image> m_images;

  ResourceUploader m_resource_uploader;

  PassPersistentConfig m_pass_cfg;
  PassPersistentResources m_pass_rcs;
  std::unique_ptr<RgPersistent> m_rgp;

  SceneData m_data;
  GpuScene m_gpu_scene;
};

} // namespace ren
