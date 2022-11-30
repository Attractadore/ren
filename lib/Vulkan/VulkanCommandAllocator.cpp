#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Support/Span.hpp"
#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

#include <range/v3/algorithm.hpp>

namespace ren {
VulkanCommandAllocator::VulkanCommandAllocator(VulkanDevice *device,
                                               unsigned pipeline_depth)
    : m_device(device) {

  m_frame_resource_lists.resize(pipeline_depth);
  m_frame_pools.reserve(pipeline_depth);
  for (int i = 0; i < pipeline_depth; ++i) {
    m_frame_pools.emplace_back(m_device);
  }

  m_frame_time = pipeline_depth - 1;
  m_frame_semaphore = m_device->createTimelineSemaphore(m_frame_time);
}

VulkanCommandAllocator::~VulkanCommandAllocator() {
  m_device->waitForSemaphore(m_frame_semaphore, m_frame_time);
  m_device->DestroySemaphore(m_frame_semaphore);
}

VulkanCommandPool &VulkanCommandAllocator::getFrameCommandPool() {
  return m_frame_pools[m_frame_index];
}

Vector<AnyRef> &VulkanCommandAllocator::getFrameResourceList() {
  return m_frame_resource_lists[m_frame_index];
}

unsigned VulkanCommandAllocator::getPipelineDepth() const {
  return m_frame_pools.size();
}

VulkanCommandBuffer *VulkanCommandAllocator::allocateVulkanCommandBuffer() {
  auto cmd_buffer = getFrameCommandPool().allocateCommandBuffer();
  return &m_frame_cmd_buffers.emplace_back(m_device, cmd_buffer, this);
}

CommandBuffer *VulkanCommandAllocator::allocateCommandBuffer() {
  return allocateVulkanCommandBuffer();
}

void VulkanCommandAllocator::beginFrame() {
  m_device->waitForSemaphore(m_frame_semaphore,
                             m_frame_time + 1 - getPipelineDepth());
  getFrameCommandPool().reset();
  getFrameResourceList().clear();
}

void VulkanCommandAllocator::endFrame() {
  m_frame_index = (m_frame_index + 1) % getPipelineDepth();
  VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_frame_semaphore,
      .value = ++m_frame_time,
  };
  VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal_info,
  };
  m_device->graphicsQueueSubmit(asSpan(submit_info));
}

void VulkanCommandAllocator::addFrameResource(AnyRef resource) {
  getFrameResourceList().push_back(std::move(resource));
}
} // namespace ren
