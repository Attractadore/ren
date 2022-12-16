#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Support/Span.hpp"
#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

namespace ren {
VulkanCommandAllocator::VulkanCommandAllocator(VulkanDevice *device,
                                               unsigned pipeline_depth) {
  m_device = device;

  m_frame_index = m_frame_number = pipeline_depth - 1;

  m_frame_pools.reserve(pipeline_depth);
  for (int i = 0; i < pipeline_depth; ++i) {
    m_frame_pools.emplace_back(m_device);
  }
  m_frame_resource_lists.resize(pipeline_depth);

  m_frame_semaphore = m_device->createTimelineSemaphore(m_frame_number);
}

VulkanCommandAllocator::~VulkanCommandAllocator() {
  m_device->waitForSemaphore(m_frame_semaphore, m_frame_number);
  m_device->DestroySemaphore(m_frame_semaphore);
}

unsigned VulkanCommandAllocator::getPipelineDepth() const {
  return m_frame_resource_lists.size();
}

VulkanCommandPool &VulkanCommandAllocator::getFrameCommandPool() {
  return m_frame_pools[m_frame_index];
}

Vector<AnyRef> &VulkanCommandAllocator::getFrameResourceList() {
  return m_frame_resource_lists[m_frame_index];
}

void VulkanCommandAllocator::addFrameResource(AnyRef resource) {
  getFrameResourceList().push_back(std::move(resource));
}

VulkanCommandBuffer *VulkanCommandAllocator::allocateVulkanCommandBuffer() {
  auto cmd_buffer = getFrameCommandPool().allocateCommandBuffer();
  return &m_frame_cmd_buffers.emplace_back(m_device, cmd_buffer, this);
}

CommandBuffer *VulkanCommandAllocator::allocateCommandBuffer() {
  return allocateVulkanCommandBuffer();
}

void VulkanCommandAllocator::beginFrameImpl() {
  ++m_frame_number;
  m_frame_index = m_frame_number % getPipelineDepth();
  m_device->waitForSemaphore(m_frame_semaphore,
                             m_frame_number - getPipelineDepth());
  getFrameCommandPool().reset();
  m_frame_cmd_buffers.clear();
  getFrameResourceList().clear();
}

void VulkanCommandAllocator::endFrameImpl() {
  VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = m_frame_semaphore,
      .value = m_frame_number,
  };
  VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal_info,
  };
  m_device->graphicsQueueSubmit(asSpan(submit_info));
}
} // namespace ren
