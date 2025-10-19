#pragma once
#include "Camera.hpp"
#include "DescriptorAllocator.hpp"
#include "Mesh.hpp"
#include "PipelineLoading.hpp"
#include "RenderGraph.hpp"
#include "ResourceUploader.hpp"
#include "Texture.hpp"
#include "passes/Pass.hpp"
#include "ren/core/GenArray.hpp"
#include "ren/ren.hpp"
#include "sh/Lighting.h"
#include "sh/PostProcessing.h"

struct ImGuiContext;

namespace ren {

constexpr usize NUM_FRAMES_IN_FLIGHT = 2;

constexpr usize MIN_TRANSFORM_STAGING_BUFFER_SIZE = 1024;

struct Image {
  Handle<Texture> handle;
};

struct Material {
  sh::Material data;
};

struct DirectionalLight {
  sh::DirectionalLight data;
};

struct DrawSetBatchDesc {
  Handle<GraphicsPipeline> pipeline;
  BufferSlice<u8> indices;

public:
  bool operator==(const DrawSetBatchDesc &) const = default;
};

struct DrawSetBatch {
  DrawSetBatchDesc desc;
  u32 num_meshlets = 0;
};

struct DrawSetData {
  DynamicArray<Handle<MeshInstance>> items;
  BufferSlice<sh::DrawSetItem> data;
  DynamicArray<DrawSetBatch> batches;
};

struct DrawSetItemUpdate {
  DrawSetId id;
  sh::DrawSetItem data;
};

struct DrawSetUpdate {
  DynamicArray<DrawSetItemUpdate> update;
  DynamicArray<DrawSetId> remove;
  DynamicArray<DrawSetItemUpdate> overwrite;
};

struct GpuScene {
  BufferSlice<float> exposure;
  BufferSlice<sh::Mesh> meshes;
  BufferSlice<sh::MeshInstance> mesh_instances;
  BufferSlice<sh::MeshInstanceVisibilityMask> mesh_instance_visibility;
  DrawSetData draw_sets[NUM_DRAW_SETS];
  BufferSlice<sh::Material> materials;

public:
  static GpuScene init(NotNull<ResourceArena *> arena);
};

struct GpuSceneMeshUpdate {
  Handle<Mesh> handle;
  sh::Mesh data;
};

struct GpuSceneMeshInstanceUpdate {
  Handle<MeshInstance> handle;
  sh::MeshInstance data;
};

struct GpuSceneMaterialUpdate {
  Handle<Material> handle;
  sh::Material data;
};

struct GpuSceneDirectionalLightUpdate {
  Handle<Material> handle;
  sh::DirectionalLight data;
};

struct GpuSceneUpdate {
  DrawSetUpdate draw_sets[NUM_DRAW_SETS];
  DynamicArray<GpuSceneMeshUpdate> meshes;
  DynamicArray<GpuSceneMeshInstanceUpdate> mesh_instances;
  DynamicArray<GpuSceneMaterialUpdate> materials;
};

struct RgDrawSetData {
  RgBufferId<sh::DrawSetItem> items;
};

struct RgGpuScene {
  RgBufferId<float> exposure;
  RgBufferId<sh::Mesh> meshes;
  RgBufferId<sh::MeshInstance> mesh_instances;
  RgBufferId<glm::mat4x3> transform_matrices;
  RgBufferId<sh::MeshInstanceVisibilityMask> mesh_instance_visibility;
  RgDrawSetData draw_sets[NUM_DRAW_SETS];
  RgBufferId<sh::Material> materials;
  RgBufferId<sh::DirectionalLight> directional_lights;
};

struct FrameResources {
  Handle<Semaphore> acquire_semaphore;
  UploadBumpAllocator upload_allocator;
  Handle<CommandPool> gfx_cmd_pool;
  Handle<CommandPool> async_cmd_pool;
  DescriptorAllocatorScope descriptor_allocator;
  Handle<Semaphore> end_semaphore;
  u64 end_time = 0;
};

struct SceneGraphicsSettings {
  bool async_compute = false;
  bool present_from_compute = false;

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

  /// Middle gray control.
  i32 brightness = 50;
  float middle_gray = 0.0f;
  sh::ExposureMode exposure_mode = sh::EXPOSURE_MODE_AUTOMATIC;
  /// Manual exposure setting in f-stops.
  float manual_exposure = 0.0f;
  /// Exposure compensation applied in all modes, in f-stops.
  float exposure_compensation = 0.0f;
  /// Automatic exposure temporal adaptation.
  /// Bright to dark and dark to bright adaptation time is 20 and 5 minutes
  /// respectively for humans, set it to be 60 times quicker for a more
  /// noticeable effect.
  bool temporal_adaptation = true;
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
  bool local_tone_mapping = true;
  float ltm_shadows = 1.5f;
  float ltm_highlights = 2.0f;
  float ltm_sigma = 3.0f;
  bool ltm_contrast_boost = true;
  i32 ltm_pyramid_size = 6;
  i32 ltm_llm_mip = 1;

  /// Dithering

  bool dithering = true;

  bool amd_anti_lag = true;
};

// Data that can change between hot reloads.
struct SceneInternalData {
  ResourceArena m_rcs_arena;
  Pipelines m_pipelines;
  DeviceBumpAllocator m_gfx_allocator;
  DeviceBumpAllocator m_async_allocator;
  std::array<DeviceBumpAllocator, 2> m_shared_allocators;
  std::array<FrameResources, NUM_FRAMES_IN_FLIGHT> m_per_frame_resources;
  PassPersistentConfig m_pass_cfg;
  PassPersistentResources m_pass_rcs;
  RgPersistent m_rgp;
  DynamicArray<UploadBumpAllocator::Allocation<glm::mat4x3>>
      m_transform_staging_buffers;
  ResourceUploader m_resource_uploader;
};

struct Scene {
  Arena *m_arena = nullptr;
  Arena m_internal_arena;
  Arena m_rg_arena;
  Renderer *m_renderer = nullptr;
  SwapChain *m_swap_chain = nullptr;
  ResourceArena m_rcs_arena;
  DescriptorAllocator m_descriptor_allocator;

  SceneGraphicsSettings m_settings;

  u64 m_frame_index = 0;
  float m_delta_time = 0.0f;
  FrameResources *m_frcs = nullptr;

  Handle<Camera> m_camera;
  GenArray<Camera> m_cameras;

  DynamicArray<IndexPool> m_index_pools;
  GenArray<Mesh> m_meshes;

  GenArray<MeshInstance> m_mesh_instances;

  GenArray<Image> m_images;

  GenArray<Material> m_materials;

  GenArray<DirectionalLight> m_directional_lights;

  glm::vec3 m_environment_luminance = {};
  sh::Handle<sh::SamplerCube> m_environment_map;

  GpuScene m_gpu_scene;
  GpuSceneUpdate m_gpu_scene_update;

  SceneInternalData *m_sid = nullptr;
};

} // namespace ren
