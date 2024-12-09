#include "DescriptorAllocator.hpp"
#include "Renderer.hpp"

namespace ren {

DescriptorAllocator::DescriptorAllocator(VkDescriptorSet set,
                                         Handle<DescriptorSetLayout> layout) {
  m_set = set;
  m_layout = layout;
}

auto DescriptorAllocator::get_set() const -> VkDescriptorSet { return m_set; }

auto DescriptorAllocator::get_set_layout() const
    -> Handle<DescriptorSetLayout> {
  return m_layout;
}

auto DescriptorAllocator::allocate_sampler(
    Renderer &renderer, Handle<Sampler> sampler) -> glsl::SamplerState {
  u32 index = m_samplers.allocate();
  ren_assert(index < glsl::NUM_SAMPLERS);

  VkDescriptorImageInfo image = {
      .sampler = renderer.get_sampler(sampler).handle,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::SAMPLERS_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = &image,
  }});

  return glsl::SamplerState(index);
};

auto DescriptorAllocator::try_allocate_sampler(
    Renderer &renderer, Handle<Sampler> sampler,
    glsl::SamplerState id) -> glsl::SamplerState {
  u32 index = m_samplers.allocate(u32(id));
  if (!index) {
    return {};
  }
  ren_assert(index == u32(id));

  VkDescriptorImageInfo image = {
      .sampler = renderer.get_sampler(sampler).handle,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::SAMPLERS_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = &image,
  }});

  return id;
};

auto DescriptorAllocator::allocate_sampler(
    Renderer &renderer, Handle<Sampler> sampler,
    glsl::SamplerState id) -> glsl::SamplerState {
  glsl::SamplerState new_id = try_allocate_sampler(renderer, sampler, id);
  ren_assert(new_id == id);
  return id;
};

void DescriptorAllocator::free_sampler(glsl::SamplerState sampler) {
  m_samplers.free(u32(sampler));
}

auto DescriptorAllocator::allocate_texture(
    Renderer &renderer, const TextureView &view) -> glsl::Texture {
  u32 index = m_textures.allocate();
  ren_assert(index < glsl::NUM_TEXTURES);

  VkDescriptorImageInfo image = {
      .imageView = renderer.getVkImageView(view),
      .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::TEXTURES_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .pImageInfo = &image,
  }});

  return glsl::Texture(index);
};

void DescriptorAllocator::free_texture(glsl::Texture texture) {
  m_textures.free(u32(texture));
}

auto DescriptorAllocator::allocate_sampled_texture(
    Renderer &renderer, const TextureView &view,
    Handle<Sampler> sampler) -> glsl::SampledTexture {
  u32 index = m_sampled_textures.allocate();
  ren_assert(index < glsl::NUM_SAMPLED_TEXTURES);

  VkDescriptorImageInfo image = {
      .sampler = renderer.get_sampler(sampler).handle,
      .imageView = renderer.getVkImageView(view),
      .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::SAMPLED_TEXTURES_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &image,
  }});

  return glsl::SampledTexture(index);
};

void DescriptorAllocator::free_sampled_texture(glsl::SampledTexture texture) {
  m_sampled_textures.free(unsigned(texture));
}

auto DescriptorAllocator::allocate_storage_texture(
    Renderer &renderer, const TextureView &view) -> glsl::StorageTexture {
  u32 index = m_storage_textures.allocate();
  ren_assert(index < glsl::NUM_STORAGE_TEXTURES);

  VkDescriptorImageInfo image = {
      .imageView = renderer.getVkImageView(view),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  renderer.write_descriptor_sets({{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::STORAGE_TEXTURES_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &image,
  }});

  return glsl::StorageTexture(index);
};

void DescriptorAllocator::free_storage_texture(glsl::StorageTexture texture) {
  m_storage_textures.free(unsigned(texture));
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
  m_textures = std::move(other.m_textures);
  m_sampled_textures = std::move(other.m_sampled_textures);
  m_storage_textures = std::move(other.m_storage_textures);
  return *this;
}

auto DescriptorAllocatorScope::get_set() const -> VkDescriptorSet {
  return m_alloc->get_set();
}

auto DescriptorAllocatorScope::get_set_layout() const
    -> Handle<DescriptorSetLayout> {
  return m_alloc->get_set_layout();
}

auto DescriptorAllocatorScope::allocate_sampler(
    Renderer &renderer, Handle<Sampler> sampler) -> glsl::SamplerState {
  return m_samplers.emplace_back(m_alloc->allocate_sampler(renderer, sampler));
}

auto DescriptorAllocatorScope::allocate_texture(
    Renderer &renderer, const TextureView &view) -> glsl::Texture {
  return m_textures.emplace_back(m_alloc->allocate_texture(renderer, view));
}

auto DescriptorAllocatorScope::allocate_sampled_texture(
    Renderer &renderer, const TextureView &view,
    Handle<Sampler> sampler) -> glsl::SampledTexture {
  return m_sampled_textures.emplace_back(
      m_alloc->allocate_sampled_texture(renderer, view, sampler));
}

auto DescriptorAllocatorScope::allocate_storage_texture(
    Renderer &renderer, const TextureView &view) -> glsl::StorageTexture {
  return m_storage_textures.emplace_back(
      m_alloc->allocate_storage_texture(renderer, view));
}

void DescriptorAllocatorScope::reset() {
  for (glsl::SamplerState sampler : m_samplers) {
    m_alloc->free_sampler(sampler);
  }
  for (glsl::Texture texture : m_textures) {
    m_alloc->free_texture(texture);
  }
  for (glsl::SampledTexture texture : m_sampled_textures) {
    m_alloc->free_sampled_texture(texture);
  }
  m_sampled_textures.clear();
  for (glsl::StorageTexture texture : m_storage_textures) {
    m_alloc->free_storage_texture(texture);
  }
  m_storage_textures.clear();
}

}; // namespace ren
