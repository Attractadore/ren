#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Support/Span.hpp"
#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

namespace ren {
VulkanCommandAllocator::VulkanCommandAllocator(VulkanDevice *device,
                                               uint64_t pipeline_depth)
    : CommandAllocator(pipeline_depth), m_device(device) {

  m_frame_pools.reserve(pipeline_depth);
  for (int i = 0; i < pipeline_depth; ++i) {
    m_frame_pools.emplace_back(m_device);
  }

  m_frame_semaphore = m_device->createTimelineSemaphore(getFrameNumber());
}

VulkanCommandAllocator::~VulkanCommandAllocator() {
  m_device->waitForSemaphore(m_frame_semaphore, getFrameNumber());
  m_device->DestroySemaphore(m_frame_semaphore);
}

VulkanCommandPool &VulkanCommandAllocator::getFrameCommandPool() {
  return m_frame_pools[getFrameIndex()];
}

VulkanCommandBuffer *VulkanCommandAllocator::allocateVulkanCommandBuffer() {
  auto cmd_buffer = getFrameCommandPool().allocateCommandBuffer();
  return &m_frame_cmd_buffers.emplace_back(m_device, cmd_buffer, this);
}

CommandBuffer *VulkanCommandAllocator::allocateCommandBuffer() {
  return allocateVulkanCommandBuffer();
}

void VulkanCommandAllocator::waitForFrame(uint64_t frame) {
  m_device->waitForSemaphore(m_frame_semaphore, frame);
}

void VulkanCommandAllocator::beginFrameImpl() { getFrameCommandPool().reset(); }

void VulkanCommandAllocator::endFrameImpl() {
  VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_frame_semaphore,
      .value = getFrameNumber(),
  };
  VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal_info,
  };
  m_device->graphicsQueueSubmit(asSpan(submit_info));
}
} // namespace ren
