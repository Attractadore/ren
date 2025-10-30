#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"

namespace ren {

namespace {

u32 free_list_allocate(NotNull<DynamicArray<u32> *> free_list,
                       NotNull<u32 *> top) {
  if (free_list->m_size > 1) {
    return free_list->pop();
  }
  return (*top)++;
}

} // namespace

DescriptorAllocator DescriptorAllocator::init(NotNull<Arena *> arena) {
  return {.m_arena = arena};
}

auto DescriptorAllocator::allocate_sampler(Renderer &renderer,
                                           rhi::Sampler sampler)
    -> sh::Handle<sh::SamplerState> {
  u32 index = free_list_allocate(&m_sampler_free_list, &m_num_samplers);
  ren_assert(index < sh::MAX_NUM_SAMPLERS);
  rhi::write_sampler_descriptor_heap(renderer.get_rhi_device(), {sampler},
                                     index);
  return sh::Handle<sh::SamplerState>(index);
};

#if 0
auto DescriptorAllocator::try_allocate_sampler(
    Renderer &renderer, rhi::Sampler sampler,
    sh::Handle<sh::SamplerState> handle) -> sh::Handle<sh::SamplerState> {
  u32 index = m_sampler_allocator.allocate(handle.m_id);
  if (!index) {
    return {};
  }
  ren_assert(index == handle.m_id);
  rhi::write_sampler_descriptor_heap(renderer.get_rhi_device(), {sampler},
                                     index);
  return handle;
};

auto DescriptorAllocator::allocate_sampler(Renderer &renderer,
                                           rhi::Sampler sampler,
                                           sh::Handle<sh::SamplerState> handle)
    -> sh::Handle<sh::SamplerState> {
  sh::Handle<sh::SamplerState> new_handle =
      try_allocate_sampler(renderer, sampler, handle);
  ren_assert(new_handle == handle);
  return handle;
};
#endif

void DescriptorAllocator::free_sampler(sh::Handle<sh::SamplerState> handle) {
  m_sampler_free_list.push(m_arena, handle.m_id);
}

auto DescriptorAllocator::allocate_texture(Renderer &renderer, SrvDesc desc)
    -> sh::Handle<void> {
  u32 index = free_list_allocate(&m_srv_free_list, &m_num_srvs);
  rhi::write_srv_descriptor_heap(renderer.get_rhi_device(),
                                 {renderer.get_srv(desc)}, index);
  return sh::Handle(index, sh::DescriptorKind::Texture);
};

void DescriptorAllocator::free_texture(sh::Handle<void> handle) {
  ren_assert(handle.m_kind == sh::DescriptorKind::Texture);
  m_srv_free_list.push(m_arena, handle.m_id);
}

auto DescriptorAllocator::allocate_sampled_texture(Renderer &renderer,
                                                   SrvDesc desc,
                                                   rhi::Sampler sampler)
    -> sh::Handle<void> {
  u32 index = free_list_allocate(&m_cis_free_list, &m_num_cis);
  rhi::write_cis_descriptor_heap(renderer.get_rhi_device(),
                                 {renderer.get_srv(desc)}, {sampler}, index);
  return sh::Handle(index, sh::DescriptorKind::Sampler);
};

void DescriptorAllocator::free_sampled_texture(sh::Handle<void> handle) {
  ren_assert(handle.m_kind == sh::DescriptorKind::Sampler);
  m_cis_free_list.push(m_arena, handle.m_id);
}

auto DescriptorAllocator::allocate_storage_texture(Renderer &renderer,
                                                   UavDesc desc)
    -> sh::Handle<void> {
  u32 index = free_list_allocate(&m_uav_free_list, &m_num_uavs);
  rhi::write_uav_descriptor_heap(renderer.get_rhi_device(),
                                 {renderer.get_uav(desc)}, index);
  return sh::Handle(index, sh::DescriptorKind::RWTexture);
};

void DescriptorAllocator::free_storage_texture(sh::Handle<void> handle) {
  ren_assert(handle.m_kind == sh::DescriptorKind::RWTexture);
  m_uav_free_list.push(m_arena, handle.m_id);
}

DescriptorAllocatorScope
DescriptorAllocatorScope::init(NotNull<DescriptorAllocator *> allocator) {
  return {.m_allocator = allocator};
}

auto DescriptorAllocatorScope::allocate_sampler(Renderer &renderer,
                                                rhi::Sampler sampler)
    -> sh::Handle<sh::SamplerState> {
  sh::Handle<sh::SamplerState> handle =
      m_allocator->allocate_sampler(renderer, sampler);
  m_sampler.push(m_allocator->m_arena, handle.m_id);
  return handle;
}

auto DescriptorAllocatorScope::allocate_texture(Renderer &renderer, SrvDesc srv)
    -> sh::Handle<void> {
  sh::Handle<void> handle = m_allocator->allocate_texture(renderer, srv);
  m_srv.push(m_allocator->m_arena, handle.m_id);
  return handle;
}

auto DescriptorAllocatorScope::allocate_sampled_texture(Renderer &renderer,
                                                        SrvDesc srv,
                                                        rhi::Sampler sampler)
    -> sh::Handle<void> {
  sh::Handle<void> handle =
      m_allocator->allocate_sampled_texture(renderer, srv, sampler);
  m_cis.push(m_allocator->m_arena, handle.m_id);
  return handle;
}

auto DescriptorAllocatorScope::allocate_storage_texture(Renderer &renderer,
                                                        UavDesc uav)
    -> sh::Handle<void> {
  sh::Handle<void> handle =
      m_allocator->allocate_storage_texture(renderer, uav);
  m_uav.push(m_allocator->m_arena, handle.m_id);
  return handle;
}

void DescriptorAllocatorScope::reset() {
  m_allocator->m_srv_free_list.push(m_allocator->m_arena, m_srv.m_data,
                                    m_srv.m_size);
  m_allocator->m_cis_free_list.push(m_allocator->m_arena, m_cis.m_data,
                                    m_cis.m_size);
  m_allocator->m_uav_free_list.push(m_allocator->m_arena, m_uav.m_data,
                                    m_uav.m_size);
  m_allocator->m_sampler_free_list.push(m_allocator->m_arena, m_sampler.m_data,
                                        m_sampler.m_size);
  m_srv.clear();
  m_cis.clear();
  m_uav.clear();
  m_sampler.clear();
}
}; // namespace ren
