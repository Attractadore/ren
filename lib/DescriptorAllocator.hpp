#pragma once
#include "Descriptors.hpp"
#include "FreeListAllocator.hpp"
#include "core/GenIndex.hpp"
#include "core/Result.hpp"
#include "glsl/Texture.h"
#include "ren/ren.hpp"

namespace ren {

class Renderer;

class ResourceArena;
struct Sampler;
struct SrvDesc;
struct UavDesc;

class DescriptorAllocator {
  Handle<ResourceDescriptorHeap> m_resource_descriptor_heap;
  Handle<SamplerDescriptorHeap> m_sampler_descriptor_heap;
  FreeListAllocator m_srv_allocator;
  FreeListAllocator m_cis_allocator;
  FreeListAllocator m_uav_allocator;
  FreeListAllocator m_sampler_allocator;

public:
  auto init(ResourceArena &arena) -> Result<void, Error>;

  auto get_resource_heap() const -> Handle<ResourceDescriptorHeap> {
    return m_resource_descriptor_heap;
  }

  auto get_sampler_heap() const -> Handle<SamplerDescriptorHeap> {
    return m_sampler_descriptor_heap;
  }

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler)
      -> glsl::SamplerState;

  auto try_allocate_sampler(Renderer &renderer, Handle<Sampler> sampler,
                            glsl::SamplerState id) -> glsl::SamplerState;

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler,
                        glsl::SamplerState id) -> glsl::SamplerState;

  void free_sampler(glsl::SamplerState sampler);

  auto allocate_texture(Renderer &renderer, SrvDesc srv)
      -> Result<glsl::Texture, Error>;

  void free_texture(glsl::Texture texture);

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                Handle<Sampler> sampler)
      -> Result<glsl::SampledTexture, Error>;

  void free_sampled_texture(glsl::SampledTexture texture);

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> Result<glsl::StorageTexture, Error>;

  void free_storage_texture(glsl::StorageTexture texture);
};

class DescriptorAllocatorScope {
public:
  DescriptorAllocatorScope(DescriptorAllocator &alloc);
  DescriptorAllocatorScope(const DescriptorAllocatorScope &) = delete;
  DescriptorAllocatorScope(DescriptorAllocatorScope &&other) = default;
  ~DescriptorAllocatorScope();

  DescriptorAllocatorScope &
  operator=(const DescriptorAllocatorScope &) = delete;
  DescriptorAllocatorScope &
  operator=(DescriptorAllocatorScope &&other) noexcept;

  auto get_resource_descriptor_heap() const -> Handle<ResourceDescriptorHeap> {
    return m_alloc->get_resource_heap();
  }

  auto get_sampler_descriptor_heap() const -> Handle<SamplerDescriptorHeap> {
    return m_alloc->get_sampler_heap();
  }

  auto allocate_sampler(Renderer &renderer, Handle<Sampler> sampler)
      -> glsl::SamplerState;

  auto allocate_texture(Renderer &renderer, SrvDesc srv)
      -> Result<glsl::Texture, Error>;

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                Handle<Sampler> sampler)
      -> Result<glsl::SampledTexture, Error>;

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> Result<glsl::StorageTexture, Error>;

  void reset();

private:
  DescriptorAllocator *m_alloc = nullptr;
  Vector<glsl::Texture> m_srv;
  Vector<glsl::SampledTexture> m_cis;
  Vector<glsl::StorageTexture> m_uav;
  Vector<glsl::SamplerState> m_sampler;
};

}; // namespace ren
