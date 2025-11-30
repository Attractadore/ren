#pragma once
#include "Renderer.hpp"
#include "Texture.hpp"
#include "ren/core/Array.hpp"
#include "ren/ren.hpp"
#include "sh/Std.h"

namespace ren {

struct Renderer;

struct ResourceArena;
struct SrvDesc;
struct UavDesc;

namespace detail {

template <typename Self> struct DescriptorAllocatorMixin {
  Self &self() { return *static_cast<Self *>(this); }
  const Self &self() const { return *static_cast<const Self *>(this); }

  template <sh::DescriptorOfKind<sh::DescriptorKind::Sampler> D>
  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler) -> sh::Handle<D> {
    if constexpr (std::same_as<D, sh::Sampler2D>) {
      srv.dimension = rhi::ImageViewDimension::e2D;
    } else if constexpr (std::same_as<D, sh::SamplerCube>) {
      srv.dimension = rhi::ImageViewDimension::eCube;
    } else if constexpr (std::same_as<D, sh::Sampler3D>) {
      srv.dimension = rhi::ImageViewDimension::e3D;
    }
    return sh::Handle<D>(
        self().allocate_sampled_texture(renderer, srv, sampler));
  }

  template <sh::DescriptorOfKind<sh::DescriptorKind::Sampler> D>
  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                const rhi::SamplerCreateInfo &sampler_info)
      -> sh::Handle<D> {
    return self().template allocate_sampled_texture<D>(
        renderer, srv, renderer.get_sampler(sampler_info));
  }
};

} // namespace detail

struct DescriptorAllocator
    : public detail::DescriptorAllocatorMixin<DescriptorAllocator> {
  Arena *m_arena = nullptr;
  u32 m_num_srvs = 1;
  u32 m_num_cis = 1;
  u32 m_num_uavs = 1;
  u32 m_num_samplers = 1;
  DynamicArray<u32> m_srv_free_list;
  DynamicArray<u32> m_cis_free_list;
  DynamicArray<u32> m_uav_free_list;
  DynamicArray<u32> m_sampler_free_list;

public:
  [[nodiscard]] static DescriptorAllocator init(NotNull<Arena *> arena);

  auto allocate_sampler(Renderer &renderer, rhi::Sampler sampler)
      -> sh::Handle<sh::SamplerState>;

#if 0
  auto try_allocate_sampler(Renderer &renderer, rhi::Sampler sampler,
                            sh::Handle<sh::SamplerState> id)
      -> sh::Handle<sh::SamplerState>;

  auto allocate_sampler(Renderer &renderer, rhi::Sampler sampler,
                        sh::Handle<sh::SamplerState> handle)
      -> sh::Handle<sh::SamplerState>;

  auto allocate_sampler(Renderer &renderer,
                        const rhi::SamplerCreateInfo &sampler_info,
                        sh::Handle<sh::SamplerState> handle)
      -> sh::Handle<sh::SamplerState> {
    return allocate_sampler(renderer,
                            renderer.get_sampler(sampler_info).value(), handle);
  }
#endif

  void free_sampler(sh::Handle<sh::SamplerState> sampler);

  auto allocate_texture(Renderer &renderer, SrvDesc srv) -> sh::Handle<void>;

  void free_texture(sh::Handle<void> texture);

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler) -> sh::Handle<void>;

  using detail::DescriptorAllocatorMixin<
      DescriptorAllocator>::allocate_sampled_texture;

  void free_sampled_texture(sh::Handle<void> texture);

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> sh::Handle<void>;

  void free_storage_texture(sh::Handle<void> texture);
};

struct DescriptorAllocatorScope
    : public detail::DescriptorAllocatorMixin<DescriptorAllocatorScope> {
  DescriptorAllocator *m_allocator = nullptr;
  DynamicArray<u32> m_srv;
  DynamicArray<u32> m_cis;
  DynamicArray<u32> m_uav;
  DynamicArray<u32> m_sampler;

public:
  [[nodiscard]] static DescriptorAllocatorScope
  init(NotNull<DescriptorAllocator *> allocator);

  auto allocate_sampler(Renderer &renderer, rhi::Sampler sampler)
      -> sh::Handle<sh::SamplerState>;

  auto allocate_texture(Renderer &renderer, SrvDesc srv) -> sh::Handle<void>;

  auto allocate_sampled_texture(Renderer &renderer, SrvDesc srv,
                                rhi::Sampler sampler) -> sh::Handle<void>;

  using detail::DescriptorAllocatorMixin<
      DescriptorAllocatorScope>::allocate_sampled_texture;

  auto allocate_storage_texture(Renderer &renderer, UavDesc uav)
      -> sh::Handle<void>;

  void reset();
};

}; // namespace ren
