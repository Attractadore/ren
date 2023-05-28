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

[[nodiscard]] auto
allocate_descriptor_pool_and_set(Device &device, ResourceArena &arena,
                                 Handle<DescriptorSetLayout> layout)
    -> std::tuple<Handle<DescriptorPool>, VkDescriptorSet>;

} // namespace ren
