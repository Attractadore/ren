#pragma once
#include "AssetLoader.hpp"
#include "BufferPool.hpp"
#include "Camera.hpp"
#include "CommandAllocator.hpp"
#include "CommandBuffer.hpp"
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
#include "ren/ren.h"

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
  static MeshMap::key_type get_mesh_key(RenMesh mesh) {
    return std::bit_cast<MeshMap::key_type>(mesh - 1);
  }

  static RenMesh get_mesh_id(MeshMap::key_type mesh_key) {
    return std::bit_cast<RenMesh>(std::bit_cast<RenMesh>(mesh_key) + 1);
  }

  auto get_mesh(RenMesh mesh) const -> const Mesh &;
  auto get_mesh(RenMesh mesh) -> Mesh &;

  static MaterialMap::key_type get_material_key(RenMaterial material) {
    return std::bit_cast<MaterialMap::key_type>(material - 1);
  }

  static RenMaterial get_material_id(MaterialMap::key_type material_key) {
    return std::bit_cast<RenMaterial>(std::bit_cast<RenMaterial>(material_key) +
                                      1);
  }

  auto get_material(RenMaterial material) const -> const Material &;
  auto get_material(RenMaterial material) -> Material &;

  static MeshInstanceMap::key_type get_model_key(RenMeshInst model) {
    return std::bit_cast<MeshInstanceMap::key_type>(model - 1);
  }

  static RenMeshInst get_model_id(MeshInstanceMap::key_type model_key) {
    return std::bit_cast<RenMeshInst>(std::bit_cast<RenMeshInst>(model_key) +
                                      1);
  }

  auto get_model(RenMeshInst model) const -> const Model &;
  auto get_model(RenMeshInst model) -> Model &;

  static DirLightMap::key_type get_dir_light_key(RenDirLight dir_light) {
    return std::bit_cast<DirLightMap::key_type>(dir_light - 1);
  }

  static RenDirLight get_dir_light_id(DirLightMap::key_type dir_light_key) {
    return std::bit_cast<RenDirLight>(
        std::bit_cast<RenDirLight>(dir_light_key) + 1);
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

  RenMesh create_mesh(const RenMeshDesc &desc);
  void destroy_mesh(RenMesh mesh);

  RenMaterial create_material(const RenMaterialDesc &desc);
  void destroy_material(RenMaterial material);

  void set_camera(const RenCameraDesc &desc) noexcept;

  auto create_model(const RenMeshInstDesc &desc) -> RenMeshInst;
  void destroy_model(RenMeshInst model);

  void set_model_matrix(RenMeshInst model, const glm::mat4 &matrix) noexcept;

  auto get_dir_light(RenDirLight dir_light) const -> const hlsl::DirLight &;
  auto get_dir_light(RenDirLight dir_light) -> hlsl::DirLight &;

  auto create_dir_light(const RenDirLightDesc &desc) -> RenDirLight;
  void destroy_dir_light(RenDirLight light);

  void draw(Swapchain &swapchain);
};
} // namespace ren

struct RenScene : ren::Scene {
  using ren::Scene::Scene;
};
