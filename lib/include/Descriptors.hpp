#pragma once
#include "Buffer.hpp"
#include "Handle.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <array>

namespace ren {

class Device;
class ResourceArena;

constexpr size_t DESCRIPTOR_TYPE_COUNT =
    VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;
constexpr size_t MAX_DESCIPTOR_BINDINGS = 16;
constexpr size_t MAX_DESCRIPTOR_SETS = 4;

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

class DescriptorSetWriter {
  Device *m_device = nullptr;
  VkDescriptorSet m_set = nullptr;
  Handle<DescriptorSetLayout> m_layout;
  SmallVector<VkDescriptorBufferInfo, MAX_DESCIPTOR_BINDINGS> m_buffers;
  SmallVector<VkDescriptorImageInfo, MAX_DESCIPTOR_BINDINGS> m_images;
  SmallVector<VkWriteDescriptorSet, MAX_DESCIPTOR_BINDINGS> m_data;

public:
  DescriptorSetWriter(Device &device, VkDescriptorSet set,
                      Handle<DescriptorSetLayout> layout);

public:
  auto add_buffer(unsigned slot, const BufferView &buffer, unsigned offset = 0)
      -> DescriptorSetWriter &;

private:
  auto add_texture_and_sampler(unsigned slot, VkImageView view,
                               VkSampler sampler, unsigned offset)
      -> DescriptorSetWriter &;

public:
  auto add_texture(unsigned slot, const TextureView &view, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto add_sampler(unsigned slot, Handle<Sampler> sampler, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto add_texture_and_sampler(unsigned slot, const TextureView &view,
                               Handle<Sampler> sampler, unsigned offset = 0)
      -> DescriptorSetWriter &;

  auto write() -> VkDescriptorSet;
};

[[nodiscard]] auto
allocate_descriptor_pool_and_set(Device &device, ResourceArena &arena,
                                 Handle<DescriptorSetLayout> layout)
    -> std::tuple<Handle<DescriptorPool>, VkDescriptorSet>;

} // namespace ren
