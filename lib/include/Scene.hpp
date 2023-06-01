#pragma once
#include "AssetLoader.hpp"
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "DenseHandleMap.hpp"
#include "ExposureOptions.hpp"
#include "MaterialPipelineCompiler.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "PipelineLoading.hpp"
#include "PostprocessingOptions.hpp"
#include "ResourceArena.hpp"
#include "ResourceUploader.hpp"
#include "Support/DenseSlotMap.hpp"
#include "TextureIDAllocator.hpp"
#include "glsl/lighting.hpp"

namespace ren {

template <> struct Hash<RenSampler> {
  auto operator()(const RenSampler &sampler) const noexcept -> usize;
};

class Swapchain;

class Scene {
  Device *m_device;

  ResourceUploader m_resource_uploader;
  Vector<BufferView> m_staged_vertex_buffers;
  Vector<BufferView> m_staged_index_buffers;
  Vector<Handle<Texture>> m_staged_textures;
  MaterialPipelineCompiler m_compiler;
  CommandAllocator m_cmd_allocator;

  Camera m_camera;
  ExposureOptions m_exposure_opts;
  PostprocessingOptions m_pp_opts;

  HandleMap<Mesh> m_meshes;

  HashMap<RenSampler, Handle<Sampler>> m_samplers;

  TextureIDAllocator m_texture_allocator;

  Vector<glsl::Material> m_materials = {{}};
  Vector<Handle<GraphicsPipeline>> m_material_pipelines = {{}};

  VkFormat m_rt_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  VkFormat m_depth_format = VK_FORMAT_D32_SFLOAT;

public:
  unsigned m_viewport_width = 1280;
  unsigned m_viewport_height = 720;

private:
  DenseHandleMap<MeshInst> m_mesh_insts;

  Vector<Handle<Texture>> m_images = {{}};

  Handle<PipelineLayout> m_pipeline_layout;

  Handle<DescriptorPool> m_persistent_descriptor_pool;
  Handle<DescriptorSetLayout> m_persistent_descriptor_set_layout;
  VkDescriptorSet m_persistent_descriptor_set = nullptr;

  DenseHandleMap<glsl::DirLight> m_dir_lights;

  ResourceArena m_persistent_arena;
  std::array<ResourceArena, 2> m_frame_arenas;
  u32 m_frame_arena_index = 0;
  u32 m_next_frame_arena_index = 1;

  Pipelines m_pipelines;

private:
  void next_frame();

  auto get_frame_arena() -> ResourceArena &;
  auto get_next_frame_arena() -> ResourceArena &;

private:
  Scene(Device &device, ResourceArena persistent_arena,
        Handle<DescriptorSetLayout> persistent_descriptor_set_layout,
        Handle<DescriptorPool> persistent_descriptor_pool,
        VkDescriptorSet persistent_descriptor_set,
        TextureIDAllocator tex_alloc);

public:
  Scene(Device &device);

  RenMesh create_mesh(const RenMeshDesc &desc);

private:
  [[nodiscard]] auto get_or_create_sampler(const RenSampler &sampler)
      -> Handle<Sampler>;

  [[nodiscard]] auto get_or_create_texture(const RenTexture &texture)
      -> SampledTextureID;

public:
  RenImage create_image(const RenImageDesc &desc);

  void create_materials(std::span<const RenMaterialDesc> descs,
                        RenMaterial *out);

  void set_camera(const RenCameraDesc &desc) noexcept;

  void set_tone_mapping(RenToneMappingOperator oper) noexcept;

  void create_mesh_insts(std::span<const RenMeshInstDesc> desc,
                         RenMeshInst *out);
  void destroy_mesh_insts(std::span<const RenMeshInst> mesh_insts) noexcept;

  void set_mesh_inst_matrices(std::span<const RenMeshInst> mesh_insts,
                              std::span<const RenMatrix4x4> matrices) noexcept;

  void create_dir_lights(std::span<const RenDirLightDesc> descs,
                         RenDirLight *out);
  void destroy_dir_lights(std::span<const RenDirLight> lights) noexcept;

  void config_dir_lights(std::span<const RenDirLight> lights,
                         std::span<const RenDirLightDesc> descs);

  void draw(Swapchain &swapchain);
};
} // namespace ren

struct RenScene : ren::Scene {
  using ren::Scene::Scene;
};
