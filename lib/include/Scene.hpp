#pragma once
#include "BufferPool.hpp"
#include "Def.hpp"
#include "Mesh.hpp"

class RenScene {
  ren::Device *m_device;

  unsigned m_output_width = 0;
  unsigned m_output_height = 0;
  ren::Swapchain *m_swapchain = nullptr;

  ren::BufferPool m_vertex_buffer_pool;
  ren::BufferPool m_index_buffer_pool;

  using MeshMap = ren::SlotMap<ren::Mesh>;
  MeshMap m_meshes;

  static MeshMap::key_type get_mesh_key(ren::MeshID mesh) {
    return std::bit_cast<MeshMap::key_type>(mesh - 1);
  }

  static ren::MeshID get_mesh_id(MeshMap::key_type mesh_key) {
    return std::bit_cast<ren::MeshID>(mesh_key) + 1;
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

  void draw();
};
