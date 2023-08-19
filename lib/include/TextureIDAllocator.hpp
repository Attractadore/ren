#pragma once
#include "FreeListAllocator.hpp"
#include "Handle.hpp"
#include "Support/NewType.hpp"

namespace ren {

class Device;
struct DescriptorSetLayout;
struct PipelineLayout;
struct Sampler;
struct TextureView;

REN_NEW_TYPE(SamplerID, unsigned);
REN_NEW_TYPE(SampledTextureID, unsigned);
REN_NEW_TYPE(StorageTextureID, unsigned);

class TextureIDAllocator {
  Device *m_device;
  VkDescriptorSet m_set;
  Handle<DescriptorSetLayout> m_layout;
  FreeListAllocator m_sampler_allocator;
  FreeListAllocator m_sampled_texture_allocator;
  FreeListAllocator m_storage_texture_allocator;

public:
  TextureIDAllocator(Device &device, VkDescriptorSet set,
                     Handle<DescriptorSetLayout> layout);

  auto get_set() const -> VkDescriptorSet;

  auto allocate_sampled_texture(const TextureView &view,
                                Handle<Sampler> sampler) -> SampledTextureID;

  auto allocate_frame_sampled_texture(const TextureView &view,
                                      Handle<Sampler> sampler)
      -> SampledTextureID;

  void free_sampled_texture(SampledTextureID texture);

  auto allocate_storage_texture(const TextureView &view) -> StorageTextureID;

  auto allocate_frame_storage_texture(const TextureView &view)
      -> StorageTextureID;

  void free_storage_texture(StorageTextureID texture);

  void next_frame();
};

}; // namespace ren
