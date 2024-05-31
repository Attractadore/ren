#include "TextureIdAllocator.hpp"
#include "Renderer.hpp"
#include "Support/Errors.hpp"
#include "glsl/Textures.hpp"

namespace ren {

TextureIdAllocator::TextureIdAllocator(VkDescriptorSet set,
                                       Handle<DescriptorSetLayout> layout) {
  m_set = set;
  m_layout = layout;
}

auto TextureIdAllocator::get_set() const -> VkDescriptorSet { return m_set; }

auto TextureIdAllocator::get_set_layout() const -> Handle<DescriptorSetLayout> {
  return m_layout;
}

auto TextureIdAllocator::allocate_sampled_texture(Renderer &renderer,
                                                  const TextureView &view,
                                                  Handle<Sampler> sampler)
    -> SampledTextureId {
  auto index = m_sampled_texture_allocator.allocate();
  assert(index < glsl::NUM_SAMPLED_TEXTURES);

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

  return SampledTextureId(index);
};

void TextureIdAllocator::free_sampled_texture(SampledTextureId texture) {
  m_sampled_texture_allocator.free(texture);
}

auto TextureIdAllocator::allocate_storage_texture(Renderer &renderer,
                                                  const TextureView &view)
    -> StorageTextureId {
  auto index = m_storage_texture_allocator.allocate();
  assert(index < glsl::NUM_STORAGE_TEXTURES);

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

  return StorageTextureId(index);
};

void TextureIdAllocator::free_storage_texture(StorageTextureId texture) {
  m_storage_texture_allocator.free(texture);
}

void TextureIdAllocator::next_frame() {
  m_sampler_allocator.next_frame();
  m_sampled_texture_allocator.next_frame();
  m_storage_texture_allocator.next_frame();
}

TextureIdAllocatorScope::TextureIdAllocatorScope(TextureIdAllocator &alloc) {
  m_alloc = &alloc;
}

TextureIdAllocatorScope::~TextureIdAllocatorScope() { clear(); }

TextureIdAllocatorScope &
TextureIdAllocatorScope::operator=(TextureIdAllocatorScope &&other) noexcept {
  clear();
  m_alloc = other.m_alloc;
  m_sampled_textures = std::move(other.m_sampled_textures);
  m_storage_textures = std::move(other.m_storage_textures);
  return *this;
}

auto TextureIdAllocatorScope::get_set() const -> VkDescriptorSet {
  return m_alloc->get_set();
}

auto TextureIdAllocatorScope::get_set_layout() const
    -> Handle<DescriptorSetLayout> {
  return m_alloc->get_set_layout();
}

auto TextureIdAllocatorScope::allocate_sampled_texture(Renderer &renderer,
                                                       const TextureView &view,
                                                       Handle<Sampler> sampler)
    -> SampledTextureId {
  return m_sampled_textures.emplace_back(
      m_alloc->allocate_sampled_texture(renderer, view, sampler));
}

auto TextureIdAllocatorScope::allocate_storage_texture(Renderer &renderer,
                                                       const TextureView &view)
    -> StorageTextureId {
  return m_storage_textures.emplace_back(
      m_alloc->allocate_storage_texture(renderer, view));
}

void TextureIdAllocatorScope::clear() {
  for (SampledTextureId texture : m_sampled_textures) {
    m_alloc->free_sampled_texture(texture);
  }
  m_sampled_textures.clear();
  for (StorageTextureId texture : m_storage_textures) {
    m_alloc->free_storage_texture(texture);
  }
  m_storage_textures.clear();
}

}; // namespace ren
