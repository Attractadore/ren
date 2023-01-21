#pragma once
#include "Buffer.hpp"
#include "ShaderStages.hpp"
#include "Support/Enum.hpp"
#include "Support/Handle.hpp"
#include "Support/Vector.hpp"
#include "Texture.hpp"

#include <vulkan/vulkan.h>

#include <array>

namespace ren {

#define REN_DESCRIPTOR_POOL_OPTIONS (UpdateAfterBind)
REN_DEFINE_FLAGS_ENUM(DescriptorPoolOption, REN_DESCRIPTOR_POOL_OPTIONS);

constexpr auto DESCRIPTOR_TYPE_COUNT = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1;

struct DescriptorPoolDesc {
  DescriptorPoolOptionFlags flags;
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

#define REN_DESCRIPTOR_SET_LAYOUT_OPTIONS (UpdateAfterBind)
REN_DEFINE_FLAGS_ENUM(DescriptorSetLayoutOption,
                      REN_DESCRIPTOR_SET_LAYOUT_OPTIONS);

#define REN_DESCRIPTOR_SET_BINDING_OPTIONS                                     \
  (UpdateAfterBind)(UpdateUnusedWhilePending)(                                 \
      PartiallyBound)(VariableDescriptorCount)
REN_DEFINE_FLAGS_ENUM(DescriptorBindingOption,
                      REN_DESCRIPTOR_SET_BINDING_OPTIONS);

struct DescriptorBinding {
  DescriptorBindingOptionFlags flags;
  unsigned binding;
  VkDescriptorType type;
  unsigned count;
  ShaderStageFlags stages;
};

struct DescriptorSetLayoutDesc {
  DescriptorSetLayoutOptionFlags flags;
  Vector<DescriptorBinding> bindings;
};

struct DescriptorSetLayoutRef {
  DescriptorSetLayoutDesc *desc;
  VkDescriptorSetLayout handle;
};

struct DescriptorSetLayout {
  std::shared_ptr<DescriptorSetLayoutDesc> desc;
  SharedHandle<VkDescriptorSetLayout> handle;

  operator DescriptorSetLayoutRef() const {
    return {.desc = desc.get(), .handle = handle.get()};
  }
};

} // namespace ren
