#pragma once
#include "Descriptors.hpp"

#include <vulkan/vulkan.h>

namespace ren {

REN_MAP_TYPE(Descriptor, VkDescriptorType);
REN_MAP_FIELD(Descriptor::Sampler, VK_DESCRIPTOR_TYPE_SAMPLER);
REN_MAP_FIELD(Descriptor::UniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
REN_MAP_FIELD(Descriptor::StorageBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
REN_MAP_FIELD(Descriptor::SampledTexture, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
REN_MAP_FIELD(Descriptor::StorageTexture, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
REN_MAP_ENUM(getVkDescriptorType, Descriptor, REN_DESCRIPTORS);
REN_REVERSE_MAP_ENUM(getDescriptor, Descriptor, REN_DESCRIPTORS);

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

inline VkDescriptorSetLayout
getVkDescriptorSetLayout(DescriptorSetLayoutRef layout) {
  return reinterpret_cast<VkDescriptorSetLayout>(layout.handle);
}

} // namespace ren
