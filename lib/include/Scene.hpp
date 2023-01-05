#pragma once
#include "BufferPool.hpp"
#include "Camera.hpp"
#include "CommandBuffer.hpp"
#include "Def.hpp"
#include "Material.hpp"
#include "MaterialAllocator.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "PipelineCompiler.hpp"
#include "ResourceUploader.hpp"

class RenScene {
  ren::Device *m_device;

  ren::Format m_rt_format = ren::Format::RGBA16F;
  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  ren::Swapchain *m_swapchain = nullptr;

  ren::BufferPool m_vertex_buffer_pool;
  ren::BufferPool m_index_buffer_pool;

  ren::Camera m_camera;

  using MeshMap = ren::SlotMap<ren::Mesh>;
  MeshMap m_meshes;

  static MeshMap::key_type get_mesh_key(ren::MeshID mesh) {
    return std::bit_cast<MeshMap::key_type>(mesh - 1);
  }

  static ren::MeshID get_mesh_id(MeshMap::key_type mesh_key) {
    return std::bit_cast<ren::MeshID>(mesh_key) + 1;
  }

  ren::MaterialAllocator m_material_allocator;

  using MaterialMap = ren::SlotMap<ren::Material>;
  MaterialMap m_materials;

  static MaterialMap::key_type get_material_key(ren::MaterialID material) {
    return std::bit_cast<MaterialMap::key_type>(material - 1);
  }

  static ren::MaterialID get_material_id(MaterialMap::key_type material_key) {
    return std::bit_cast<ren::MaterialID>(material_key) + 1;
  }

  using ModelMap = ren::SlotMap<ren::Model>;
  ModelMap m_models;

  static ModelMap::key_type get_model_key(ren::ModelID model) {
    return std::bit_cast<ModelMap::key_type>(model - 1);
  }

  static ren::ModelID get_model_id(ModelMap::key_type model_key) {
    return std::bit_cast<ren::ModelID>(model_key) + 1;
  }

  ren::ResourceUploader m_resource_uploader;

private:
  void begin_frame();
  void end_frame();

  auto get_mesh(ren::MeshID mesh) const -> const ren::Mesh &;
  auto get_mesh(ren::MeshID mesh) -> ren::Mesh &;

  auto get_material(ren::MaterialID material) const -> const ren::Material &;
  auto get_material(ren::MaterialID material) -> ren::Material &;

  auto get_model(ren::ModelID model) const -> const ren::Model &;
  auto get_model(ren::ModelID model) -> ren::Model &;

public:
  RenScene(ren::Device *device);
  ~RenScene();

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

  void draw();
};
