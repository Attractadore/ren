#pragma once
#include "FreeListAllocator.hpp"
#include "Texture.hpp"
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
  FreeListAllocator m_srv_allocator;
  FreeListAllocator m_cis_allocator;
  FreeListAllocator m_uav_allocator;
  FreeListAllocator m_sampler_allocator;

public:
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

  template <std::constructible_from<glsl::SampledTexture> D>
  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                Handle<Sampler> sampler) -> Result<D, Error> {
    if constexpr (std::same_as<D, glsl::SampledTexture2D>) {
      srv.dimension = rhi::ImageViewDimension::e2D;
    } else if constexpr (std::same_as<D, glsl::SampledTextureCube>) {
      srv.dimension = rhi::ImageViewDimension::eCube;
    }
    ren_try(glsl::SampledTexture desc,
            allocate_sampled_texture(renderer, srv, sampler));
    return D(desc);
  }

  void free_sampled_texture(glsl::SampledTexture texture);

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> Result<glsl::StorageTexture, Error>;

  void free_storage_texture(glsl::StorageTexture texture);
};

class DescriptorAllocatorScope {
public:
  DescriptorAllocatorScope() = default;
  DescriptorAllocatorScope(const DescriptorAllocatorScope &) = delete;
  DescriptorAllocatorScope(DescriptorAllocatorScope &&other) = default;
  ~DescriptorAllocatorScope();

  DescriptorAllocatorScope &
  operator=(const DescriptorAllocatorScope &) = delete;
  DescriptorAllocatorScope &
  operator=(DescriptorAllocatorScope &&other) noexcept;

  auto init(DescriptorAllocator &allocator) -> Result<void, Error>;

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
  DescriptorAllocator *m_allocator = nullptr;
  Vector<glsl::Texture> m_srv;
  Vector<glsl::SampledTexture> m_cis;
  Vector<glsl::StorageTexture> m_uav;
  Vector<glsl::SamplerState> m_sampler;
};

}; // namespace ren
