#include "TextureIDAllocator.hpp"
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
  DescriptorSetWriter(*m_device, m_set, m_layout)
      .add_texture_and_sampler(glsl::SAMPLED_TEXTURES_SLOT, view, sampler,
                               index)
      .write();
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
  DescriptorSetWriter(*m_device, m_set, m_layout)
      .add_texture(glsl::STORAGE_TEXTURES_SLOT, view, index)
      .write();
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
