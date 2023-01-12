#pragma once
#include "Buffer.hpp"

#include <vulkan/vulkan.h>

namespace ren {

REN_MAP_TYPE(BufferUsage, VkBufferUsageFlagBits);
REN_ENUM_FLAGS(VkBufferUsageFlagBits, VkBufferUsageFlags);
REN_MAP_FIELD(BufferUsage::TransferSRC, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
REN_MAP_FIELD(BufferUsage::TransferDST, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
REN_MAP_FIELD(BufferUsage::UniformTexel,
              VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::StorageTexel,
              VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::Uniform, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::Storage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::RWStorage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::Index, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::Vertex, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::Indirect, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
REN_MAP_FIELD(BufferUsage::DeviceAddress,
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
REN_MAP_ENUM_AND_FLAGS(getVkBufferUsage, BufferUsage, REN_BUFFER_USAGES);

inline VkBuffer getVkBuffer(BufferRef buffer) {
  return reinterpret_cast<VkBuffer>(buffer.handle);
}

} // namespace ren
