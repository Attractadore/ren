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

} // namespace ren
