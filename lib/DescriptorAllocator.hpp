#pragma once
#include "FreeListAllocator.hpp"
#include "Renderer.hpp"
#include "Texture.hpp"
#include "core/Result.hpp"
#include "glsl/Texture.h"
#include "ren/ren.hpp"

namespace ren {

class Renderer;

class ResourceArena;
struct SrvDesc;
struct UavDesc;

namespace detail {

struct DescriptorAllocatorMixin {
  template <std::constructible_from<glsl::SampledTexture> D>
  auto allocate_sampled_texture(this auto &self, Renderer &renderer,
                                SrvDesc srv, rhi::Sampler sampler)
      -> Result<D, Error> {
    if constexpr (std::same_as<D, glsl::SampledTexture2D>) {
      srv.dimension = rhi::ImageViewDimension::e2D;
    } else if constexpr (std::same_as<D, glsl::SampledTextureCube>) {
      srv.dimension = rhi::ImageViewDimension::eCube;
    }
    ren_try(glsl::SampledTexture desc,
            self.allocate_sampled_texture(renderer, srv, sampler));
    return D(desc);
  }

  template <std::constructible_from<glsl::SampledTexture> D>
  auto allocate_sampled_texture(this auto &self, Renderer &renderer,
                                SrvDesc srv,
                                const rhi::SamplerCreateInfo &sampler_info)
      -> Result<D, Error> {
    ren_try(rhi::Sampler sampler, renderer.get_sampler(sampler_info));
    return self.template allocate_sampled_texture<D>(renderer, srv, sampler);
  }
};

} // namespace detail

class DescriptorAllocator : public detail::DescriptorAllocatorMixin {
  FreeListAllocator m_srv_allocator;
  FreeListAllocator m_cis_allocator;
  FreeListAllocator m_uav_allocator;
  FreeListAllocator m_sampler_allocator;

public:
  auto allocate_sampler(Renderer &renderer, rhi::Sampler sampler)
      -> glsl::SamplerState;

  auto try_allocate_sampler(Renderer &renderer, rhi::Sampler sampler,
                            glsl::SamplerState id) -> glsl::SamplerState;

  auto allocate_sampler(Renderer &renderer, rhi::Sampler sampler,
                        glsl::SamplerState id) -> glsl::SamplerState;

  void free_sampler(glsl::SamplerState sampler);

  auto allocate_texture(Renderer &renderer, SrvDesc srv)
      -> Result<glsl::Texture, Error>;

  void free_texture(glsl::Texture texture);

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler)
      -> Result<glsl::SampledTexture, Error>;

  using detail::DescriptorAllocatorMixin::allocate_sampled_texture;

  void free_sampled_texture(glsl::SampledTexture texture);

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> Result<glsl::StorageTexture, Error>;

  void free_storage_texture(glsl::StorageTexture texture);
};

class DescriptorAllocatorScope : public detail::DescriptorAllocatorMixin {
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

  auto allocate_sampler(Renderer &renderer, rhi::Sampler sampler)
      -> glsl::SamplerState;

  auto allocate_texture(Renderer &renderer, SrvDesc srv)
      -> Result<glsl::Texture, Error>;

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler)
      -> Result<glsl::SampledTexture, Error>;

  using detail::DescriptorAllocatorMixin::allocate_sampled_texture;

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
