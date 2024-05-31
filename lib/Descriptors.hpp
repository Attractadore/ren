#pragma once
#include "Config.hpp"
#include "DebugNames.hpp"
#include "Handle.hpp"

#include <array>
#include <vulkan/vulkan.h>

namespace ren {

class Renderer;
class ResourceArena;

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
allocate_descriptor_pool_and_set(Renderer &renderer, ResourceArena &arena,
                                 Handle<DescriptorSetLayout> layout)
    -> std::tuple<Handle<DescriptorPool>, VkDescriptorSet>;

} // namespace ren
