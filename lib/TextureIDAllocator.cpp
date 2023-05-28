#include "TextureIDAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "glsl/interface.hpp"

namespace ren {

TextureIDAllocator::TextureIDAllocator(Device &device, VkDescriptorSet set,
                                       Handle<DescriptorSetLayout> layout)
    : m_device(&device), m_set(set), m_layout(layout) {}

auto TextureIDAllocator::get_set() const -> VkDescriptorSet { return m_set; }

auto TextureIDAllocator::allocate_sampled_texture(const TextureView &view,
                                                  Handle<Sampler> sampler)
    -> SampledTextureID {
  auto index = m_sampled_texture_allocator.allocate();
  assert(index < glsl::NUM_SAMPLED_TEXTURES);

  VkDescriptorImageInfo image = {
      .sampler = m_device->get_sampler(sampler).handle,
      .imageView = m_device->getVkImageView(view),
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  VkWriteDescriptorSet config = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::SAMPLED_TEXTURES_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &image,
  };
  m_device->write_descriptor_set(config);

  return SampledTextureID(index);
};

auto TextureIDAllocator::allocate_frame_sampled_texture(const TextureView &view,
                                                        Handle<Sampler> sampler)
    -> SampledTextureID {
  auto index = allocate_sampled_texture(view, sampler);
  m_sampled_texture_allocator.free(index);
  return index;
};

auto TextureIDAllocator::allocate_storage_texture(const TextureView &view)
    -> StorageTextureID {
  auto index = m_storage_texture_allocator.allocate();
  assert(index < glsl::NUM_STORAGE_TEXTURES);

  VkDescriptorImageInfo image = {
      .imageView = m_device->getVkImageView(view),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  VkWriteDescriptorSet config = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = m_set,
      .dstBinding = glsl::STORAGE_TEXTURES_SLOT,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .pImageInfo = &image,
  };
  m_device->write_descriptor_set(config);

  return StorageTextureID(index);
};

auto TextureIDAllocator::allocate_frame_storage_texture(const TextureView &view)
    -> StorageTextureID {
  auto index = allocate_storage_texture(view);
  m_storage_texture_allocator.free(index);
  return index;
};

void TextureIDAllocator::next_frame() {
  m_sampler_allocator.next_frame();
  m_sampled_texture_allocator.next_frame();
  m_storage_texture_allocator.next_frame();
}

}; // namespace ren
