#pragma once
#include "AssetLoader.hpp"
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSetAllocator.hpp"
#include "MaterialPipelineCompiler.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "ResourceArena.hpp"
#include "ResourceUploader.hpp"
#include "Support/DenseSlotMap.hpp"
#include "Support/NewType.hpp"
#include "hlsl/interface.hpp"
#include "hlsl/lighting.hpp"
#include "ren/ren.h"

namespace ren {

class Swapchain;

using MeshMap = DenseSlotMap<Mesh>;
using MeshInstanceMap = DenseSlotMap<MeshInst>;
using DirLightMap = DenseSlotMap<hlsl::DirLight>;

REN_NEW_TYPE(SamplerID, unsigned);
REN_NEW_TYPE(TextureID, unsigned);

class Scene {
  Device *m_device;

  AssetLoader m_asset_loader;

  ResourceUploader m_resource_uploader;
  Vector<Handle<Buffer>> m_staged_vertex_buffers;
  Vector<Handle<Buffer>> m_staged_index_buffers;
  Vector<Handle<Texture>> m_staged_textures;
  MaterialPipelineCompiler m_compiler;
  DescriptorSetAllocator m_descriptor_set_allocator;
  CommandAllocator m_cmd_allocator;

  Camera m_camera;

  MeshMap m_meshes;

  Vector<SamplerDesc> m_sampler_descs;
  Vector<Sampler> m_samplers;

  unsigned m_num_textures = 1;

  Vector<hlsl::Material> m_materials = {{}};
  Vector<GraphicsPipelineRef> m_material_pipelines = {{}};

  VkFormat m_rt_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  VkFormat m_depth_format = VK_FORMAT_D32_SFLOAT;

public:
  unsigned m_viewport_width = 1280;
  unsigned m_viewport_height = 720;

private:
  MeshInstanceMap m_mesh_insts;

  Vector<Handle<Texture>> m_images = {{}};

  PipelineLayout m_pipeline_layout = {};

  DescriptorPool m_persistent_descriptor_pool = {};
  VkDescriptorSet m_persistent_descriptor_set = {};

  DirLightMap m_dir_lights;

  ResourceArena m_persistent_arena;
  ResourceArena m_frame_arena;

private:
  void next_frame();

public:
  Scene(Device &device);
  ~Scene();

  RenMesh create_mesh(const RenMeshDesc &desc);

private:
  [[nodiscard]] auto get_or_create_sampler(const RenTexture &texture)
      -> SamplerID;

  [[nodiscard]] auto get_or_create_texture(const RenTexture &texture)
      -> TextureID;

public:
  RenImage create_image(const RenImageDesc &desc);

  void create_materials(std::span<const RenMaterialDesc> descs,
                        RenMaterial *out);

  void set_camera(const RenCameraDesc &desc) noexcept;

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
