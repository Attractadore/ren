#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"
#include "ResourceArena.hpp"

namespace ren {

auto DescriptorAllocator::init(ResourceArena &arena) -> Result<void, Error> {
  ren_try(
      m_resource_descriptor_heap,
      arena.create_resource_descriptor_heap(ResourceDescriptorHeapCreateInfo{
          .name = "Resource descriptor heap",
          .num_srv_descriptors = glsl::MAX_NUM_RESOURCES / 3,
          .num_cis_descriptors = glsl::MAX_NUM_RESOURCES / 3,
          .num_uav_descriptors = glsl::MAX_NUM_RESOURCES / 3,
      }));
  ren_try(m_sampler_descriptor_heap, arena.create_sampler_descriptor_heap(
                                         {.name = "Sampler descriptor heap"}));
  return {};
}

auto DescriptorAllocator::allocate_sampler(Renderer &renderer,
                                           Handle<Sampler> sampler)
    -> glsl::SamplerState {
  u32 index = m_sampler_allocator.allocate();
  ren_assert(index < glsl::MAX_NUM_SAMPLERS);
  rhi::write_sampler_descriptor_heap(
      renderer.get_rhi_device(),
      renderer.get_sampler_descriptor_heap(m_sampler_descriptor_heap).handle,
      {renderer.get_sampler(sampler).handle}, index);
  return glsl::SamplerState(index);
};

auto DescriptorAllocator::try_allocate_sampler(Renderer &renderer,
                                               Handle<Sampler> sampler,
                                               glsl::SamplerState id)
    -> glsl::SamplerState {
  u32 index = m_sampler_allocator.allocate(u32(id));
  if (!index) {
    return {};
  }
  ren_assert(index == u32(id));
  rhi::write_sampler_descriptor_heap(
      renderer.get_rhi_device(),
      renderer.get_sampler_descriptor_heap(m_sampler_descriptor_heap).handle,
      {renderer.get_sampler(sampler).handle}, index);
  return id;
};

auto DescriptorAllocator::allocate_sampler(Renderer &renderer,
                                           Handle<Sampler> sampler,
                                           glsl::SamplerState id)
    -> glsl::SamplerState {
  glsl::SamplerState new_id = try_allocate_sampler(renderer, sampler, id);
  ren_assert(new_id == id);
  return id;
};

void DescriptorAllocator::free_sampler(glsl::SamplerState sampler) {
  m_sampler_allocator.free(u32(sampler));
}

auto DescriptorAllocator::allocate_texture(Renderer &renderer, SrvDesc desc)
    -> Result<glsl::Texture, Error> {
  u32 index = m_srv_allocator.allocate();
  ren_try(rhi::SRV srv, renderer.get_srv(desc));
  rhi::write_resource_descriptor_heap(
      renderer.get_rhi_device(),
      renderer.get_resource_descriptor_heap(m_resource_descriptor_heap).handle,
      {srv}, index);
  return glsl::Texture(index);
};

void DescriptorAllocator::free_texture(glsl::Texture texture) {
  m_srv_allocator.free(u32(texture));
}

auto DescriptorAllocator::allocate_sampled_texture(Renderer &renderer,
                                                   SrvDesc desc,
                                                   Handle<Sampler> sampler)
    -> Result<glsl::SampledTexture, Error> {
  u32 index = m_cis_allocator.allocate();
  ren_try(rhi::SRV srv, renderer.get_srv(desc));
  rhi::write_resource_descriptor_heap(
      renderer.get_rhi_device(),
      renderer.get_resource_descriptor_heap(m_resource_descriptor_heap).handle,
      {srv}, {renderer.get_sampler(sampler).handle}, index);
  return glsl::SampledTexture(index);
};

void DescriptorAllocator::free_sampled_texture(glsl::SampledTexture texture) {
  m_cis_allocator.free(unsigned(texture));
}

auto DescriptorAllocator::allocate_storage_texture(Renderer &renderer,
                                                   UavDesc desc)
    -> Result<glsl::StorageTexture, Error> {
  u32 index = m_uav_allocator.allocate();
  ren_try(rhi::UAV uav, renderer.get_uav(desc));
  rhi::write_resource_descriptor_heap(
      renderer.get_rhi_device(),
      renderer.get_resource_descriptor_heap(m_resource_descriptor_heap).handle,
      {uav}, index);
  return glsl::StorageTexture(index);
};

void DescriptorAllocator::free_storage_texture(glsl::StorageTexture texture) {
  m_uav_allocator.free(unsigned(texture));
}

DescriptorAllocatorScope::DescriptorAllocatorScope(DescriptorAllocator &alloc) {
  m_alloc = &alloc;
}

DescriptorAllocatorScope::~DescriptorAllocatorScope() { reset(); }

DescriptorAllocatorScope &
DescriptorAllocatorScope::operator=(DescriptorAllocatorScope &&other) noexcept {
  reset();
  m_alloc = other.m_alloc;
  m_sampler = std::move(other.m_sampler);
  m_srv = std::move(other.m_srv);
  m_cis = std::move(other.m_cis);
  m_uav = std::move(other.m_uav);
  return *this;
}

auto DescriptorAllocatorScope::allocate_sampler(Renderer &renderer,
                                                Handle<Sampler> sampler)
    -> glsl::SamplerState {
  return m_sampler.emplace_back(m_alloc->allocate_sampler(renderer, sampler));
}

auto DescriptorAllocatorScope::allocate_texture(Renderer &renderer, SrvDesc srv)
    -> Result<glsl::Texture, Error> {
  ren_try(glsl::Texture texture, m_alloc->allocate_texture(renderer, srv));
  return m_srv.emplace_back(texture);
}

auto DescriptorAllocatorScope::allocate_sampled_texture(Renderer &renderer,
                                                        SrvDesc srv,
                                                        Handle<Sampler> sampler)
    -> Result<glsl::SampledTexture, Error> {
  ren_try(glsl::SampledTexture texture,
          m_alloc->allocate_sampled_texture(renderer, srv, sampler));
  return m_cis.emplace_back(texture);
}

auto DescriptorAllocatorScope::allocate_storage_texture(Renderer &renderer,
                                                        UavDesc uav)
    -> Result<glsl::StorageTexture, Error> {
  ren_try(glsl::StorageTexture texture,
          m_alloc->allocate_storage_texture(renderer, uav));
  return m_uav.emplace_back(texture);
}

void DescriptorAllocatorScope::reset() {
  for (glsl::Texture texture : m_srv) {
    m_alloc->free_texture(texture);
  }
  for (glsl::SampledTexture texture : m_cis) {
    m_alloc->free_sampled_texture(texture);
  }
  for (glsl::StorageTexture texture : m_uav) {
    m_alloc->free_storage_texture(texture);
  }
  for (glsl::SamplerState sampler : m_sampler) {
    m_alloc->free_sampler(sampler);
  }
  m_srv.clear();
  m_cis.clear();
  m_uav.clear();
  m_sampler.clear();
}

}; // namespace ren
