#pragma once
#include "Buffer.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <array>

namespace ren {

constexpr auto DESCRIPTOR_TYPE_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;

struct DescriptorPoolDesc {
  VkDescriptorPoolCreateFlags flags;
  unsigned set_count;
  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes;
};

struct DescriptorPoolRef {
  DescriptorPoolDesc desc;
  VkDescriptorPool handle;
};

struct DescriptorPool {
  DescriptorPoolDesc desc;
  SharedHandle<VkDescriptorPool> handle;

  operator DescriptorPoolRef() const {
    return {.desc = desc, .handle = handle.get()};
  }
};

constexpr size_t MAX_DESCIPTOR_BINDINGS = 16;

struct DescriptorBinding {
  VkDescriptorBindingFlags flags;
  VkDescriptorType type;
  unsigned count;
  VkShaderStageFlags stages;
};

struct DescriptorSetLayoutDesc {
  VkDescriptorSetLayoutCreateFlags flags;
  std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS> bindings;
};

struct DescriptorSetLayoutRef {
  DescriptorSetLayoutDesc *desc;
  VkDescriptorSetLayout handle;
};

constexpr size_t MAX_DESCIPTOR_SETS = 4;

struct DescriptorSetLayout {
  std::shared_ptr<DescriptorSetLayoutDesc> desc;
  SharedHandle<VkDescriptorSetLayout> handle;

  operator DescriptorSetLayoutRef() const {
    return {.desc = desc.get(), .handle = handle.get()};
  }
};

class Device;

class DescriptorSetWriter {
  Device *m_device;
  DescriptorSetLayoutRef m_layout;
  VkDescriptorSet m_set;
  SmallVector<VkDescriptorBufferInfo, MAX_DESCIPTOR_BINDINGS> m_buffers;
  SmallVector<VkDescriptorImageInfo, MAX_DESCIPTOR_BINDINGS> m_images;
  SmallVector<VkWriteDescriptorSet, MAX_DESCIPTOR_BINDINGS> m_data;

public:
  DescriptorSetWriter(Device &device, VkDescriptorSet set,
                      DescriptorSetLayoutRef layout);

  auto add_buffer(unsigned slot, const BufferHandleView &buffer,
                  unsigned offset = 0) -> DescriptorSetWriter &;

  auto add_buffer(unsigned slot, Handle<Buffer> buffer, unsigned offset = 0)
      -> DescriptorSetWriter &;

private:
  auto add_texture_and_sampler(unsigned slot, VkImageView view,
                               VkSampler sampler, unsigned offset)
      -> DescriptorSetWriter &;

public:
  auto add_texture(unsigned slot, const TextureHandleView &view,
                   unsigned offset = 0) -> DescriptorSetWriter &;

  auto add_sampler(unsigned slot, const SamplerRef &sampler,
                   unsigned offset = 0) -> DescriptorSetWriter &;

  auto add_texture_and_sampler(unsigned slot, const TextureHandleView &view,
                               const SamplerRef &sampler, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto write() -> VkDescriptorSet;
};

} // namespace ren
