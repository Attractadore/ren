#pragma once
#include "FreeListAllocator.hpp"
#include "Renderer.hpp"
#include "Texture.hpp"
#include "core/Result.hpp"
#include "ren/ren.hpp"
#include "sh/Std.h"

namespace ren {

class Renderer;

class ResourceArena;
struct SrvDesc;
struct UavDesc;

namespace detail {

struct DescriptorAllocatorMixin {
  template <sh::DescriptorOfKind<sh::DescriptorKind::Sampler> D>
  auto allocate_sampled_texture(this auto &self, Arena scratch,
                                Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler) -> sh::Handle<D> {
    if constexpr (std::same_as<D, sh::Sampler2D>) {
      srv.dimension = rhi::ImageViewDimension::e2D;
    } else if constexpr (std::same_as<D, sh::SamplerCube>) {
      srv.dimension = rhi::ImageViewDimension::eCube;
    } else if constexpr (std::same_as<D, sh::Sampler3D>) {
      srv.dimension = rhi::ImageViewDimension::e3D;
    }
    return sh::Handle<D>(
        self.allocate_sampled_texture(scratch, renderer, srv, sampler));
  }

  template <sh::DescriptorOfKind<sh::DescriptorKind::Sampler> D>
  auto allocate_sampled_texture(this auto &self, Arena scratch,
                                Renderer &renderer, SrvDesc srv,
                                const rhi::SamplerCreateInfo &sampler_info)
      -> sh::Handle<D> {
    return self.template allocate_sampled_texture<D>(
        scratch, renderer, srv, renderer.get_sampler(sampler_info).value());
  }
};

} // namespace detail

class DescriptorAllocator : public detail::DescriptorAllocatorMixin {
  FreeListAllocator m_srv_allocator;
  FreeListAllocator m_cis_allocator;
  FreeListAllocator m_uav_allocator;
  FreeListAllocator m_sampler_allocator;

public:
  auto allocate_sampler(Arena scratch, Renderer &renderer, rhi::Sampler sampler)
      -> sh::Handle<sh::SamplerState>;

  auto try_allocate_sampler(Arena scratch, Renderer &renderer,
                            rhi::Sampler sampler,
                            sh::Handle<sh::SamplerState> id)
      -> sh::Handle<sh::SamplerState>;

  auto allocate_sampler(Arena scratch, Renderer &renderer, rhi::Sampler sampler,
                        sh::Handle<sh::SamplerState> handle)
      -> sh::Handle<sh::SamplerState>;

  auto allocate_sampler(Arena scratch, Renderer &renderer,
                        const rhi::SamplerCreateInfo &sampler_info,
                        sh::Handle<sh::SamplerState> handle)
      -> sh::Handle<sh::SamplerState> {
    return allocate_sampler(scratch, renderer,
                            renderer.get_sampler(sampler_info).value(), handle);
  }

  void free_sampler(sh::Handle<sh::SamplerState> sampler);

  auto allocate_texture(Arena scratch, Renderer &renderer, SrvDesc srv)
      -> sh::Handle<void>;

  void free_texture(sh::Handle<void> texture);

  auto allocate_sampled_texture(Arena scratch, Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler) -> sh::Handle<void>;

  using detail::DescriptorAllocatorMixin::allocate_sampled_texture;

  void free_sampled_texture(sh::Handle<void> texture);

  auto allocate_storage_texture(Arena scratch, Renderer &renderer, UavDesc uav)
      -> sh::Handle<void>;

  void free_storage_texture(sh::Handle<void> texture);
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

  auto allocate_sampler(Arena scratch, Renderer &renderer, rhi::Sampler sampler)
      -> sh::Handle<sh::SamplerState>;

  auto allocate_texture(Arena scratch, Renderer &renderer, SrvDesc srv)
      -> sh::Handle<void>;

  auto allocate_sampled_texture(Arena scratch, Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler) -> sh::Handle<void>;

  using detail::DescriptorAllocatorMixin::allocate_sampled_texture;

  auto allocate_storage_texture(Arena scratch, Renderer &renderer, UavDesc uav)
      -> sh::Handle<void>;

  void reset();

private:
  DescriptorAllocator *m_allocator = nullptr;
  Vector<u32> m_srv;
  Vector<u32> m_cis;
  Vector<u32> m_uav;
  Vector<u32> m_sampler;
};

}; // namespace ren
