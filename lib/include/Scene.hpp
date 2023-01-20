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
#include "VertexFetchStrategy.hpp"

class RenScene {
  ren::Device *m_device;

  AssetLoader m_asset_loader;

  VertexFetchStrategy m_vertex_fetch;
  BufferPool m_vertex_buffer_pool;
  BufferPool m_index_buffer_pool;
  ResourceUploader m_resource_uploader;
  MaterialPipelineCompiler m_compiler;
  MaterialAllocator m_material_allocator;
  DescriptorSetAllocator m_descriptor_set_allocator;
  std::unique_ptr<CommandAllocator> m_cmd_allocator;

  ren::Format m_rt_format = ren::Format::RGBA16F;
  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  ren::Swapchain *m_swapchain = nullptr;

  ren::Camera m_camera;

  using MeshMap = ren::SlotMap<ren::Mesh>;
  MeshMap m_meshes;

  using MaterialMap = ren::SlotMap<ren::Material>;
  MaterialMap m_materials;

  using ModelMap = ren::SlotMap<ren::Model>;
  ModelMap m_models;

  PipelineSignature m_pipeline_signature = {};

  DescriptorPool m_persistent_descriptor_pool = {};
  VkDescriptorSet m_persistent_descriptor_set = {};

  BufferRef m_materials_buffer = {};

private:
  static MeshMap::key_type get_mesh_key(ren::MeshID mesh) {
    return std::bit_cast<MeshMap::key_type>(mesh - 1);
  }

  static ren::MeshID get_mesh_id(MeshMap::key_type mesh_key) {
    return std::bit_cast<ren::MeshID>(mesh_key) + 1;
  }

  auto get_mesh(ren::MeshID mesh) const -> const ren::Mesh &;
  auto get_mesh(ren::MeshID mesh) -> ren::Mesh &;

  static MaterialMap::key_type get_material_key(ren::MaterialID material) {
    return std::bit_cast<MaterialMap::key_type>(material - 1);
  }

  static ren::MaterialID get_material_id(MaterialMap::key_type material_key) {
    return std::bit_cast<ren::MaterialID>(material_key) + 1;
  }

  auto get_material(ren::MaterialID material) const -> const ren::Material &;
  auto get_material(ren::MaterialID material) -> ren::Material &;

  static ModelMap::key_type get_model_key(ren::ModelID model) {
    return std::bit_cast<ModelMap::key_type>(model - 1);
  }

  static ren::ModelID get_model_id(ModelMap::key_type model_key) {
    return std::bit_cast<ren::ModelID>(model_key) + 1;
  }

  auto get_model(ren::ModelID model) const -> const ren::Model &;
  auto get_model(ren::ModelID model) -> ren::Model &;

  DescriptorSetLayoutRef get_persistent_descriptor_set_layout() const {
    return m_pipeline_signature.desc->set_layouts[hlsl::c_persistent_set];
  }

  DescriptorSetLayoutRef get_global_descriptor_set_layout() const {
    return m_pipeline_signature.desc->set_layouts[hlsl::c_global_set];
  }

public:
  RenScene(ren::Device *device);

  void setOutputSize(unsigned width, unsigned height);
  unsigned getOutputWidth() const { return m_output_width; }
  unsigned getOutputHeight() const { return m_output_height; }

  void setSwapchain(ren::Swapchain *swapchain);
  ren::Swapchain *getSwapchain() const { return m_swapchain; }

  ren::MeshID create_mesh(const ren::MeshDesc &desc);
  void destroy_mesh(ren::MeshID mesh);

  ren::MaterialID create_material(const ren::MaterialDesc &desc);
  void destroy_material(ren::MaterialID material);

  void set_camera(const ren::CameraDesc &desc);

  auto create_model(const ren::ModelDesc &desc) -> ren::ModelID;
  void destroy_model(ren::ModelID model);

  void set_model_matrix(ren::ModelID model, const glm::mat4 &matrix);

  void begin_frame();
  void end_frame();

  void draw();
};
