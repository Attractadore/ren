#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"

namespace ren {

DescriptorAllocator::DescriptorAllocator(
    Span<const VkDescriptorSet> sets,
    const PersistentDescriptorSetLayouts &layouts) {
  std::ranges::copy(sets, m_sets.data());
  m_layouts = layouts;
}

auto DescriptorAllocator::get_sets() const -> Span<const VkDescriptorSet> {
  return m_sets;
}

auto DescriptorAllocator::allocate_sampler(Renderer &renderer,
                                           Handle<Sampler> sampler)
    -> glsl::SamplerState {
  u32 index = m_samplers.allocate();
  ren_assert(index < glsl::NUM_SAMPLERS);

  VkDescriptorImageInfo image = {
      .sampler = renderer.get_sampler(sampler).handle.handle,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_sets[glsl::SAMPLER_SET],
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = &image,
  }});

  return glsl::SamplerState(index);
};

auto DescriptorAllocator::try_allocate_sampler(Renderer &renderer,
                                               Handle<Sampler> sampler,
                                               glsl::SamplerState id)
    -> glsl::SamplerState {
  u32 index = m_samplers.allocate(u32(id));
  if (!index) {
    return {};
  }
  ren_assert(index == u32(id));

  VkDescriptorImageInfo image = {
      .sampler = renderer.get_sampler(sampler).handle.handle,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_sets[glsl::SAMPLER_SET],
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = &image,
  }});

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
  m_samplers.free(u32(sampler));
}

auto DescriptorAllocator::allocate_texture(Renderer &renderer, SrvDesc desc)
    -> Result<glsl::Texture, Error> {
  u32 index = m_srvs.allocate();

  ren_try(rhi::SRV srv, renderer.get_srv(desc));
  VkDescriptorImageInfo image = {
      .imageView = srv.handle,
      .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_sets[glsl::SRV_SET],
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .pImageInfo = &image,
  }});

  return glsl::Texture(index);
};

void DescriptorAllocator::free_texture(glsl::Texture texture) {
  m_srvs.free(u32(texture));
}

auto DescriptorAllocator::allocate_sampled_texture(Renderer &renderer,
                                                   SrvDesc desc,
                                                   Handle<Sampler> sampler)
    -> Result<glsl::SampledTexture, Error> {
  u32 index = m_ciss.allocate();

  ren_try(rhi::SRV srv, renderer.get_srv(desc));
  VkDescriptorImageInfo image = {
      .sampler = renderer.get_sampler(sampler).handle.handle,
      .imageView = srv.handle,
      .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_sets[glsl::CIS_SET],
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &image,
  }});

  return glsl::SampledTexture(index);
};

void DescriptorAllocator::free_sampled_texture(glsl::SampledTexture texture) {
  m_ciss.free(unsigned(texture));
}

auto DescriptorAllocator::allocate_storage_texture(Renderer &renderer,
                                                   UavDesc desc)
    -> Result<glsl::StorageTexture, Error> {
  u32 index = m_uavs.allocate();

  ren_try(rhi::UAV uav, renderer.get_uav(desc));
  VkDescriptorImageInfo image = {
      .imageView = uav.handle,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_sets[glsl::UAV_SET],
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &image,
  }});

  return glsl::StorageTexture(index);
};

void DescriptorAllocator::free_storage_texture(glsl::StorageTexture texture) {
  m_uavs.free(unsigned(texture));
}

DescriptorAllocatorScope::DescriptorAllocatorScope(DescriptorAllocator &alloc) {
  m_alloc = &alloc;
}

DescriptorAllocatorScope::~DescriptorAllocatorScope() { reset(); }

DescriptorAllocatorScope &
DescriptorAllocatorScope::operator=(DescriptorAllocatorScope &&other) noexcept {
  reset();
  m_alloc = other.m_alloc;
  m_samplers = std::move(other.m_samplers);
  m_srvs = std::move(other.m_srvs);
  m_ciss = std::move(other.m_ciss);
  m_uavs = std::move(other.m_uavs);
  return *this;
}

auto DescriptorAllocatorScope::get_sets() const -> Span<const VkDescriptorSet> {
  return m_alloc->get_sets();
}

auto DescriptorAllocatorScope::allocate_sampler(Renderer &renderer,
                                                Handle<Sampler> sampler)
    -> glsl::SamplerState {
  return m_samplers.emplace_back(m_alloc->allocate_sampler(renderer, sampler));
}

auto DescriptorAllocatorScope::allocate_texture(Renderer &renderer, SrvDesc srv)
    -> Result<glsl::Texture, Error> {
  ren_try(glsl::Texture texture, m_alloc->allocate_texture(renderer, srv));
  return m_srvs.emplace_back(texture);
}

auto DescriptorAllocatorScope::allocate_sampled_texture(Renderer &renderer,
                                                        SrvDesc srv,
                                                        Handle<Sampler> sampler)
    -> Result<glsl::SampledTexture, Error> {
  ren_try(glsl::SampledTexture texture,
          m_alloc->allocate_sampled_texture(renderer, srv, sampler));
  return m_ciss.emplace_back(texture);
}

auto DescriptorAllocatorScope::allocate_storage_texture(Renderer &renderer,
                                                        UavDesc uav)
    -> Result<glsl::StorageTexture, Error> {
  ren_try(glsl::StorageTexture texture,
          m_alloc->allocate_storage_texture(renderer, uav));
  return m_uavs.emplace_back(texture);
}

void DescriptorAllocatorScope::reset() {
  for (glsl::Texture texture : m_srvs) {
    m_alloc->free_texture(texture);
  }
  for (glsl::SampledTexture texture : m_ciss) {
    m_alloc->free_sampled_texture(texture);
  }
  for (glsl::StorageTexture texture : m_uavs) {
    m_alloc->free_storage_texture(texture);
  }
  for (glsl::SamplerState sampler : m_samplers) {
    m_alloc->free_sampler(sampler);
  }
  m_srvs.clear();
  m_ciss.clear();
  m_uavs.clear();
  m_samplers.clear();
}

}; // namespace ren
