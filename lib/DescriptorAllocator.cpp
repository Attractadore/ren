#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"

namespace ren {

auto DescriptorAllocator::allocate_sampler(Arena scratch, Renderer &renderer,
                                           rhi::Sampler sampler)
    -> sh::Handle<sh::SamplerState> {
  u32 index = m_sampler_allocator.allocate();
  ren_assert(index < sh::MAX_NUM_SAMPLERS);
  rhi::write_sampler_descriptor_heap(scratch, renderer.get_rhi_device(),
                                     {sampler}, index);
  return sh::Handle<sh::SamplerState>(index);
};

auto DescriptorAllocator::try_allocate_sampler(
    Arena scratch, Renderer &renderer, rhi::Sampler sampler,
    sh::Handle<sh::SamplerState> handle) -> sh::Handle<sh::SamplerState> {
  u32 index = m_sampler_allocator.allocate(handle.m_id);
  if (!index) {
    return {};
  }
  ren_assert(index == handle.m_id);
  rhi::write_sampler_descriptor_heap(scratch, renderer.get_rhi_device(),
                                     {sampler}, index);
  return handle;
};

auto DescriptorAllocator::allocate_sampler(Arena scratch, Renderer &renderer,
                                           rhi::Sampler sampler,
                                           sh::Handle<sh::SamplerState> handle)
    -> sh::Handle<sh::SamplerState> {
  sh::Handle<sh::SamplerState> new_handle =
      try_allocate_sampler(scratch, renderer, sampler, handle);
  ren_assert(new_handle == handle);
  return handle;
};

void DescriptorAllocator::free_sampler(sh::Handle<sh::SamplerState> handle) {
  m_sampler_allocator.free(handle.m_id);
}

auto DescriptorAllocator::allocate_texture(Arena scratch, Renderer &renderer,
                                           SrvDesc desc) -> sh::Handle<void> {
  u32 index = m_srv_allocator.allocate();
  rhi::write_srv_descriptor_heap(scratch, renderer.get_rhi_device(),
                                 {renderer.get_srv(desc).value()}, index);
  return sh::Handle(index, sh::DescriptorKind::Texture);
};

void DescriptorAllocator::free_texture(sh::Handle<void> handle) {
  ren_assert(handle.m_kind == sh::DescriptorKind::Texture);
  m_srv_allocator.free(handle.m_id);
}

auto DescriptorAllocator::allocate_sampled_texture(Arena scratch,
                                                   Renderer &renderer,
                                                   SrvDesc desc,
                                                   rhi::Sampler sampler)
    -> sh::Handle<void> {
  u32 index = m_cis_allocator.allocate();
  rhi::write_cis_descriptor_heap(scratch, renderer.get_rhi_device(),
                                 {renderer.get_srv(desc).value()}, {sampler},
                                 index);
  return sh::Handle(index, sh::DescriptorKind::Sampler);
};

void DescriptorAllocator::free_sampled_texture(sh::Handle<void> handle) {
  ren_assert(handle.m_kind == sh::DescriptorKind::Sampler);
  m_cis_allocator.free(handle.m_id);
}

auto DescriptorAllocator::allocate_storage_texture(Arena scratch,
                                                   Renderer &renderer,
                                                   UavDesc desc)
    -> sh::Handle<void> {
  u32 index = m_uav_allocator.allocate();
  rhi::write_uav_descriptor_heap(scratch, renderer.get_rhi_device(),
                                 {renderer.get_uav(desc).value()}, index);
  return sh::Handle(index, sh::DescriptorKind::RWTexture);
};

void DescriptorAllocator::free_storage_texture(sh::Handle<void> handle) {
  ren_assert(handle.m_kind == sh::DescriptorKind::RWTexture);
  m_uav_allocator.free(handle.m_id);
}

auto DescriptorAllocatorScope::init(DescriptorAllocator &allocator)
    -> Result<void, Error> {
  m_allocator = &allocator;
  return {};
}

DescriptorAllocatorScope::~DescriptorAllocatorScope() { reset(); }

DescriptorAllocatorScope &
DescriptorAllocatorScope::operator=(DescriptorAllocatorScope &&other) noexcept {
  reset();
  m_allocator = other.m_allocator;
  m_sampler = std::move(other.m_sampler);
  m_srv = std::move(other.m_srv);
  m_cis = std::move(other.m_cis);
  m_uav = std::move(other.m_uav);
  return *this;
}

auto DescriptorAllocatorScope::allocate_sampler(Arena scratch,
                                                Renderer &renderer,
                                                rhi::Sampler sampler)
    -> sh::Handle<sh::SamplerState> {
  sh::Handle<sh::SamplerState> handle =
      m_allocator->allocate_sampler(scratch, renderer, sampler);
  m_sampler.push_back(handle.m_id);
  return handle;
}

auto DescriptorAllocatorScope::allocate_texture(Arena scratch,
                                                Renderer &renderer, SrvDesc srv)
    -> sh::Handle<void> {
  sh::Handle<void> handle =
      m_allocator->allocate_texture(scratch, renderer, srv);
  m_srv.push_back(handle.m_id);
  return handle;
}

auto DescriptorAllocatorScope::allocate_sampled_texture(Arena scratch,
                                                        Renderer &renderer,
                                                        SrvDesc srv,
                                                        rhi::Sampler sampler)
    -> sh::Handle<void> {
  sh::Handle<void> handle =
      m_allocator->allocate_sampled_texture(scratch, renderer, srv, sampler);
  m_cis.push_back(handle.m_id);
  return handle;
}

auto DescriptorAllocatorScope::allocate_storage_texture(Arena scratch,
                                                        Renderer &renderer,
                                                        UavDesc uav)
    -> sh::Handle<void> {
  sh::Handle<void> handle =
      m_allocator->allocate_storage_texture(scratch, renderer, uav);
  m_uav.push_back(handle.m_id);
  return handle;
}

void DescriptorAllocatorScope::reset() {
  for (u32 index : m_srv) {
    m_allocator->free_texture(sh::Handle(index, sh::DescriptorKind::Texture));
  }
  for (u32 index : m_cis) {
    m_allocator->free_sampled_texture(
        sh::Handle(index, sh::DescriptorKind::Sampler));
  }
  for (u32 index : m_uav) {
    m_allocator->free_storage_texture(
        sh::Handle(index, sh::DescriptorKind::RWTexture));
  }
  for (u32 index : m_sampler) {
    m_allocator->free_sampler(sh::Handle<sh::SamplerState>(index));
  }
  m_srv.clear();
  m_cis.clear();
  m_uav.clear();
  m_sampler.clear();
}

}; // namespace ren
