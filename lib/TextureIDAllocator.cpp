#include "TextureIDAllocator.hpp"
#include "hlsl/interface.hpp"

namespace ren {

TextureIDAllocator::TextureIDAllocator(Device &device, VkDescriptorSet set,
                                       Handle<DescriptorSetLayout> layout)
    : m_device(&device), m_set(set), m_layout(layout) {}

auto TextureIDAllocator::allocate_sampler(Handle<Sampler> sampler)
    -> SamplerID {
  auto index = m_sampler_allocator.allocate();
  assert(index < hlsl::NUM_SAMPLERS);
  DescriptorSetWriter(*m_device, m_set, m_layout)
      .add_sampler(hlsl::SAMPLERS_SLOT, sampler, index)
      .write();
  return SamplerID(index);
};

auto TextureIDAllocator::get_set() const -> VkDescriptorSet { return m_set; }

auto TextureIDAllocator::allocate_sampled_texture(const TextureView &view)
    -> SampledTextureID {
  auto index = m_sampled_texture_allocator.allocate();
  assert(index < hlsl::NUM_SAMPLED_TEXTURES);
  DescriptorSetWriter(*m_device, m_set, m_layout)
      .add_texture(hlsl::SAMPLED_TEXTURES_SLOT, view, index)
      .write();
  return SampledTextureID(index);
};

auto TextureIDAllocator::allocate_frame_sampled_texture(const TextureView &view)
    -> SampledTextureID {
  auto index = allocate_sampled_texture(view);
  m_sampled_texture_allocator.free(index);
  return index;
};

auto TextureIDAllocator::allocate_storage_texture(const TextureView &view)
    -> StorageTextureID {
  auto index = m_storage_texture_allocator.allocate();
  assert(index < hlsl::NUM_STORAGE_TEXTURES);
  DescriptorSetWriter(*m_device, m_set, m_layout)
      .add_texture(hlsl::STORAGE_TEXTURES_SLOT, view, index)
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
