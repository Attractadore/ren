#pragma once
#include "Camera.hpp"
#include "DescriptorAllocator.hpp"
#include "GpuScene.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Texture.hpp"
#include "core/GenArray.hpp"
#include "core/GenMap.hpp"
#include "passes/Pass.hpp"
#include "ren/ren.hpp"
#include "sh/PostProcessing.h"

struct ImGuiContext;

namespace ren {

using Image = Handle<Texture>;

constexpr usize NUM_FRAMES_IN_FLIGHT = 2;

struct ScenePerFrameResources {
  Handle<Semaphore> acquire_semaphore;
  UploadBumpAllocator upload_allocator;
  Handle<CommandPool> gfx_cmd_pool;
  Handle<CommandPool> async_cmd_pool;
  DescriptorAllocatorScope descriptor_allocator;
  Handle<Semaphore> end_semaphore;
  u64 end_time = 0;

public:
  auto reset(Renderer &renderer) -> Result<void, Error>;
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
  i32 ssao_num_samples = 16;
  float ssao_radius = 1.0f;
  float ssao_lod_bias = 0.0f;
  bool ssao_full_res = false;

  /// Post processing

  /// Camera

  /// Camera relative aperture size in f-stops.
  float camera_aperture = 16.0f;
  /// Inverse camera shutter time in 1/seconds.
  float inv_camera_shutter_time = 400.0f;
  /// Camera sensitivity in ISO.
  float camera_iso = 400.0f;

  /// Exposure

  sh::ExposureMode exposure_mode = sh::EXPOSURE_MODE_AUTOMATIC;
  /// Manual exposure setting in f-stops.
  float manual_exposure = 0.0f;
  /// Exposure compensation applied in all modes, in f-stops.
  float exposure_compensation = 0.0f;
  /// Automatic exposure temporal adaptation.
  /// Bright to dark and dark to bright adaptation time is 20 and 5 minutes
  /// respectively for humans, set it to be 60 times quicker for a more
  /// noticeable effect.
  float dark_adaptation_time = 20;
  float bright_adaptation_time = 5;
  /// Exposure metering mode for automatic exposure.
  sh::MeteringMode metering_mode = sh::METERING_MODE_CENTER_WEIGHTED;
  /// Spot metering pattern diameter.
  float spot_metering_pattern_relative_diameter = 0.1f;
  /// Center-weighted metering inner diameter.
  float center_weighted_metering_pattern_relative_inner_diameter = 0.3f;
  /// Center-weighted metering pattern ratio of inner and outer sizes.
  float center_weighted_metering_pattern_size_ratio = 1.5f;

  /// Tone mapping

  sh::ToneMapper tone_mapper = sh::TONE_MAPPER_AGX_PUNCHY;

  bool amd_anti_lag = true;
};

struct SceneData {
  float delta_time = 0.0f;

  SceneGraphicsSettings settings;

  Handle<Camera> camera;
  GenArray<Camera> cameras;

  IndexPoolList index_pools;
  GenArray<Mesh> meshes;

  GenArray<MeshInstance> mesh_instances;
  GenMap<glm::mat4x3, Handle<MeshInstance>> mesh_instance_transforms;

  GenArray<sh::Material> materials;

  GenArray<sh::DirectionalLight> directional_lights;

  glm::vec3 env_luminance = {};
  sh::Handle<sh::SamplerCube> env_map;

public:
  const Camera &get_camera() const {
    ren_assert(camera);
    return cameras[camera];
  }
};

// Data that can change between hot reloads.
struct SceneInternalData {
  ResourceArena m_arena;
  Pipelines m_pipelines;
  DeviceBumpAllocator m_gfx_allocator;
  DeviceBumpAllocator m_async_allocator;
  std::array<DeviceBumpAllocator, 2> m_shared_allocators;
  std::array<ScenePerFrameResources, NUM_FRAMES_IN_FLIGHT>
      m_per_frame_resources;
  PassPersistentConfig m_pass_cfg;
  PassPersistentResources m_pass_rcs;
  RgPersistent m_rgp;
};

struct Scene {
  auto get_camera(CameraId camera) -> Camera &;

  auto create_texture(const void *blob, usize size)
      -> expected<Handle<Texture>>;

  bool is_amd_anti_lag_available();

  bool is_amd_anti_lag_enabled();

  auto next_frame() -> Result<void, Error>;

  [[nodiscard]] auto get_or_create_texture(Handle<Image> image,
                                           const SamplerDesc &sampler_desc)
      -> Result<sh::Handle<sh::Sampler2D>, Error>;

  auto build_rg() -> Result<RenderGraph, Error>;

  Renderer *m_renderer = nullptr;
  SwapChain *m_swap_chain = nullptr;
  ResourceArena m_arena;
  DescriptorAllocator m_descriptor_allocator;
  u64 m_frame_index = 0;
  ScenePerFrameResources *m_frcs = nullptr;
  GenArray<Image> m_images;
  ResourceUploader m_resource_uploader;
  SceneData m_data;
  GpuScene m_gpu_scene;
  std::unique_ptr<SceneInternalData> m_sid;
};

} // namespace ren
