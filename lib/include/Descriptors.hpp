#pragma once
#include "Buffer.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <array>

namespace ren {

class ResourceArena;

constexpr auto DESCRIPTOR_TYPE_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;

struct DescriptorPoolCreateInfo {
  REN_DEBUG_NAME_FIELD("Descriptor pool");
  VkDescriptorPoolCreateFlags flags;
  unsigned set_count;
  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes;
};

struct DescriptorPool {
  VkDescriptorPool handle;
  VkDescriptorPoolCreateFlags flags;
  unsigned set_count;
  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes;
};

constexpr size_t MAX_DESCIPTOR_BINDINGS = 16;

struct DescriptorBinding {
  VkDescriptorBindingFlags flags;
  VkDescriptorType type;
  unsigned count;
  VkShaderStageFlags stages;
};

struct DescriptorSetLayoutCreateInfo {
  REN_DEBUG_NAME_FIELD("Descriptor set layout");
  VkDescriptorSetLayoutCreateFlags flags;
  std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS> bindings;
};

struct DescriptorSetLayout {
  VkDescriptorSetLayout handle;
  VkDescriptorSetLayoutCreateFlags flags;
  std::array<DescriptorBinding, MAX_DESCIPTOR_BINDINGS> bindings;
};

constexpr size_t MAX_DESCRIPTOR_SETS = 4;

class Device;

class DescriptorSetWriter {
  const DescriptorSetLayout *m_layout = nullptr;
  VkDescriptorSet m_set;
  SmallVector<VkDescriptorBufferInfo, MAX_DESCIPTOR_BINDINGS> m_buffers;
  SmallVector<VkDescriptorImageInfo, MAX_DESCIPTOR_BINDINGS> m_images;
  SmallVector<VkWriteDescriptorSet, MAX_DESCIPTOR_BINDINGS> m_data;

public:
  DescriptorSetWriter(VkDescriptorSet set, const DescriptorSetLayout &layout);

public:
  auto add_buffer(unsigned slot, const BufferView &buffer, unsigned offset = 0)
      -> DescriptorSetWriter &;

private:
  auto add_texture_and_sampler(unsigned slot, VkImageView view,
                               VkSampler sampler, unsigned offset)
      -> DescriptorSetWriter &;

public:
  auto add_texture(unsigned slot, VkImageView view, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto add_sampler(unsigned slot, const Sampler &sampler, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto add_texture_and_sampler(unsigned slot, VkImageView view,
                               const Sampler &sampler, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto write(Device &device) -> VkDescriptorSet;
};

[[nodiscard]] auto
allocate_descriptor_pool_and_set(Device &device, ResourceArena &arena,
                                 Handle<DescriptorSetLayout> layout)
    -> std::tuple<Handle<DescriptorPool>, VkDescriptorSet>;

} // namespace ren
