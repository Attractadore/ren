#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

namespace ren {
VulkanCommandAllocator::VulkanCommandAllocator(VulkanDevice &device,
                                               unsigned pipeline_depth) {
  m_device = &device;
  m_frame_pools.reserve(pipeline_depth);
  for (int i = 0; i < pipeline_depth; ++i) {
    m_frame_pools.emplace_back(*m_device);
  }
  m_frame_times.resize(pipeline_depth, 0);
}

unsigned VulkanCommandAllocator::getPipelineDepth() const {
  return m_frame_pools.size();
}

VulkanCommandBuffer *VulkanCommandAllocator::allocateVulkanCommandBuffer() {
  auto cmd_buffer = m_frame_pools[m_frame_index].allocate();
  return &m_frame_cmd_buffers.emplace_back(m_device, cmd_buffer, this);
}

CommandBuffer *VulkanCommandAllocator::allocateCommandBuffer() {
  return allocateVulkanCommandBuffer();
}

void VulkanCommandAllocator::beginFrameImpl() {
  m_device->waitForGraphicsQueue(m_frame_times[m_frame_index]);
  m_frame_pools[m_frame_index].reset();
  m_frame_cmd_buffers.clear();
}

void VulkanCommandAllocator::endFrameImpl() {
  m_frame_times[m_frame_index] = m_device->getGraphicsQueueTime();
  m_frame_index = (m_frame_index + 1) % getPipelineDepth();
}
} // namespace ren
