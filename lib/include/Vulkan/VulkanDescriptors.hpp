#pragma once
#include "Descriptors.hpp"

#include <vulkan/vulkan.h>

namespace ren {

REN_MAP_TYPE(DescriptorPoolOption, VkDescriptorPoolCreateFlagBits);
REN_ENUM_FLAGS(VkDescriptorPoolCreateFlagBits, VkDescriptorPoolCreateFlags);
REN_MAP_FIELD(DescriptorPoolOption::UpdateAfterBind,
              VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);
REN_MAP_ENUM_AND_FLAGS(getVkDescriptorPoolOption, DescriptorPoolOption,
                       REN_DESCRIPTOR_POOL_OPTIONS);

REN_MAP_TYPE(DescriptorSetLayoutOption, VkDescriptorSetLayoutCreateFlagBits);
REN_ENUM_FLAGS(VkDescriptorSetLayoutCreateFlagBits,
               VkDescriptorSetLayoutCreateFlags);
REN_MAP_FIELD(DescriptorSetLayoutOption::UpdateAfterBind,
              VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
REN_MAP_ENUM_AND_FLAGS(getVkDescriptorSetLayoutOption,
                       DescriptorSetLayoutOption,
                       REN_DESCRIPTOR_SET_LAYOUT_OPTIONS);

REN_MAP_TYPE(DescriptorBindingOption, VkDescriptorBindingFlagBits);
REN_ENUM_FLAGS(VkDescriptorBindingFlagBits, VkDescriptorBindingFlags);
REN_MAP_FIELD(DescriptorBindingOption::UpdateAfterBind,
              VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
REN_MAP_FIELD(DescriptorBindingOption::UpdateUnusedWhilePending,
              VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);
REN_MAP_FIELD(DescriptorBindingOption::PartiallyBound,
              VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
REN_MAP_FIELD(DescriptorBindingOption::VariableDescriptorCount,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);
REN_MAP_ENUM_AND_FLAGS(getVkDescriptorBindingOption, DescriptorBindingOption,
                       REN_DESCRIPTOR_SET_BINDING_OPTIONS);

inline auto getVkDescriptorPool(DescriptorPoolRef pool) -> VkDescriptorPool {
  return reinterpret_cast<VkDescriptorPool>(pool.handle);
}

inline auto getVkDescriptorSetLayout(DescriptorSetLayoutRef layout)
    -> VkDescriptorSetLayout {
  return reinterpret_cast<VkDescriptorSetLayout>(layout.handle);
}

} // namespace ren
