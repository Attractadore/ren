#pragma once
#include "AssetLoader.hpp"
#include "BufferPool.hpp"
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
#include "Def.hpp"
#include "DescriptorSetAllocator.hpp"
#include "Material.hpp"
#include "MaterialAllocator.hpp"
#include "MaterialPipelineCompiler.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "ResourceUploader.hpp"
#include "Support/SlotMap.hpp"
#include "hlsl/interface.hpp"
#include "hlsl/lighting.hpp"

namespace ren {

class Swapchain;

class Scene {
  Device *m_device;

  AssetLoader m_asset_loader;

  BufferPool m_vertex_buffer_pool;
  BufferPool m_index_buffer_pool;
  ResourceUploader m_resource_uploader;
  MaterialPipelineCompiler m_compiler;
  MaterialAllocator m_material_allocator;
  DescriptorSetAllocator m_descriptor_set_allocator;
  CommandAllocator m_cmd_allocator;

  Camera m_camera;

  using MeshMap = SlotMap<Mesh>;
  MeshMap m_meshes;

  using MaterialMap = SlotMap<Material>;
  MaterialMap m_materials;

  VkFormat m_rt_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  VkFormat m_depth_format = VK_FORMAT_D32_SFLOAT;

public:
  unsigned m_viewport_width = 1280;
  unsigned m_viewport_height = 720;

private:
  using MeshInstanceMap = SlotMap<Model>;
  MeshInstanceMap m_models;

  PipelineLayout m_pipeline_layout = {};

  DescriptorPool m_persistent_descriptor_pool = {};
  VkDescriptorSet m_persistent_descriptor_set = {};

  BufferRef m_materials_buffer = {};

  using DirLightMap = SlotMap<hlsl::DirLight>;
  DirLightMap m_dir_lights;

private:
  static MeshMap::key_type get_mesh_key(MeshID mesh) {
    return std::bit_cast<MeshMap::key_type>(mesh - 1);
  }

  static MeshID get_mesh_id(MeshMap::key_type mesh_key) {
    return std::bit_cast<MeshID>(std::bit_cast<MeshID>(mesh_key) + 1);
  }

  auto get_mesh(MeshID mesh) const -> const Mesh &;
  auto get_mesh(MeshID mesh) -> Mesh &;

  static MaterialMap::key_type get_material_key(MaterialID material) {
    return std::bit_cast<MaterialMap::key_type>(material - 1);
  }

  static MaterialID get_material_id(MaterialMap::key_type material_key) {
    return std::bit_cast<MaterialID>(std::bit_cast<MaterialID>(material_key) +
                                     1);
  }

  auto get_material(MaterialID material) const -> const Material &;
  auto get_material(MaterialID material) -> Material &;

  static MeshInstanceMap::key_type get_model_key(MeshInstanceID model) {
    return std::bit_cast<MeshInstanceMap::key_type>(model - 1);
  }

  static MeshInstanceID get_model_id(MeshInstanceMap::key_type model_key) {
    return std::bit_cast<MeshInstanceID>(
        std::bit_cast<MeshInstanceID>(model_key) + 1);
  }

  auto get_model(MeshInstanceID model) const -> const Model &;
  auto get_model(MeshInstanceID model) -> Model &;

  static DirLightMap::key_type get_dir_light_key(DirLightID dir_light) {
    return std::bit_cast<DirLightMap::key_type>(dir_light - 1);
  }

  static DirLightID get_dir_light_id(DirLightMap::key_type dir_light_key) {
    return std::bit_cast<DirLightID>(std::bit_cast<DirLightID>(dir_light_key) +
                                     1);
  }

  DescriptorSetLayoutRef get_persistent_descriptor_set_layout() const {
    return m_pipeline_layout.desc->set_layouts[hlsl::PERSISTENT_SET];
  }

  DescriptorSetLayoutRef get_global_descriptor_set_layout() const {
    return m_pipeline_layout.desc->set_layouts[hlsl::GLOBAL_SET];
  }

  void next_frame();

public:
  Scene(Device &device);

  MeshID create_mesh(const MeshDesc &desc);
  void destroy_mesh(MeshID mesh);

  MaterialID create_material(const MaterialDesc &desc);
  void destroy_material(MaterialID material);

  void set_camera(const CameraDesc &desc) noexcept;

  auto create_model(const MeshInstanceDesc &desc) -> MeshInstanceID;
  void destroy_model(MeshInstanceID model);

  void set_model_matrix(MeshInstanceID model, const glm::mat4 &matrix) noexcept;

  auto get_dir_light(DirLightID dir_light) const -> const hlsl::DirLight &;
  auto get_dir_light(DirLightID dir_light) -> hlsl::DirLight &;

  auto create_dir_light(const DirLightDesc &desc) -> DirLightID;
  void destroy_dir_light(DirLightID light);

  void draw(Swapchain &swapchain);
};
} // namespace ren

struct RenScene : ren::Scene {
  using ren::Scene::Scene;
};
