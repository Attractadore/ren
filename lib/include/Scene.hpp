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

  VkFormat m_rt_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  Swapchain *m_swapchain = nullptr;

  Camera m_camera;

  using MeshMap = SlotMap<Mesh>;
  MeshMap m_meshes;

  using MaterialMap = SlotMap<Material>;
  MaterialMap m_materials;

  using ModelMap = SlotMap<Model>;
  ModelMap m_models;

  PipelineLayout m_pipeline_layout = {};

  DescriptorPool m_persistent_descriptor_pool = {};
  VkDescriptorSet m_persistent_descriptor_set = {};

  BufferRef m_materials_buffer = {};

private:
  static MeshMap::key_type get_mesh_key(MeshID mesh) {
    return std::bit_cast<MeshMap::key_type>(mesh - 1);
  }

  static MeshID get_mesh_id(MeshMap::key_type mesh_key) {
    return std::bit_cast<MeshID>(mesh_key) + 1;
  }

  auto get_mesh(MeshID mesh) const -> const Mesh &;
  auto get_mesh(MeshID mesh) -> Mesh &;

  static MaterialMap::key_type get_material_key(MaterialID material) {
    return std::bit_cast<MaterialMap::key_type>(material - 1);
  }

  static MaterialID get_material_id(MaterialMap::key_type material_key) {
    return std::bit_cast<MaterialID>(material_key) + 1;
  }

  auto get_material(MaterialID material) const -> const Material &;
  auto get_material(MaterialID material) -> Material &;

  static ModelMap::key_type get_model_key(ModelID model) {
    return std::bit_cast<ModelMap::key_type>(model - 1);
  }

  static ModelID get_model_id(ModelMap::key_type model_key) {
    return std::bit_cast<ModelID>(model_key) + 1;
  }

  auto get_model(ModelID model) const -> const Model &;
  auto get_model(ModelID model) -> Model &;

  DescriptorSetLayoutRef get_persistent_descriptor_set_layout() const {
    return m_pipeline_layout.desc->set_layouts[hlsl::PERSISTENT_SET];
  }

  DescriptorSetLayoutRef get_global_descriptor_set_layout() const {
    return m_pipeline_layout.desc->set_layouts[hlsl::GLOBAL_SET];
  }

public:
  Scene(Device &device);

  void setOutputSize(unsigned width, unsigned height);
  unsigned getOutputWidth() const { return m_output_width; }
  unsigned getOutputHeight() const { return m_output_height; }

  void setSwapchain(Swapchain &swapchain);

  MeshID create_mesh(const MeshDesc &desc);
  void destroy_mesh(MeshID mesh);

  MaterialID create_material(const MaterialDesc &desc);
  void destroy_material(MaterialID material);

  void set_camera(const CameraDesc &desc);

  auto create_model(const ModelDesc &desc) -> ModelID;
  void destroy_model(ModelID model);

  void set_model_matrix(ModelID model, const glm::mat4 &matrix);

  void begin_frame();
  void end_frame();

  void draw();
};
} // namespace ren

struct RenScene : ren::Scene {
  using ren::Scene::Scene;
};
