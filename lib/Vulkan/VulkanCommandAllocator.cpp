#include "Vulkan/VulkanCommandAllocator.hpp"
#include "Vulkan/VulkanCommandBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

#include <range/v3/view.hpp>

namespace ren {
VulkanCommandAllocator::VulkanCommandAllocator(VulkanDevice &device) {
  m_device = &device;
  m_frame_pools =
      ranges::views::generate([&] { return VulkanCommandPool(*m_device); }) |
      ranges::views::take(m_frame_pools.max_size()) |
      ranges::to<decltype(m_frame_pools)>;
}

VulkanCommandBuffer *VulkanCommandAllocator::allocateVulkanCommandBuffer() {
  auto cmd_buffer = m_frame_pools[m_frame_index].allocate();
  return &m_frame_cmd_buffers.emplace_back(m_device, cmd_buffer, this);
}

CommandBuffer *VulkanCommandAllocator::allocateCommandBuffer() {
  return allocateVulkanCommandBuffer();
}

void VulkanCommandAllocator::begin_frame() {
  m_frame_index = (m_frame_index + 1) % m_frame_pools.size();
  m_frame_pools[m_frame_index].reset();
  m_frame_cmd_buffers.clear();
}

void VulkanCommandAllocator::end_frame() {}
} // namespace ren
