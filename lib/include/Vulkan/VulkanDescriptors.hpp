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

} // namespace ren
