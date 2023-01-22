#include "CommandAllocator.hpp"
#include "Vulkan/VulkanDevice.hpp"

#include <range/v3/view.hpp>

namespace ren {

CommandAllocator::CommandAllocator(VulkanDevice &device) {
  m_device = &device;
  m_frame_pools =
      ranges::views::generate([&] { return VulkanCommandPool(*m_device); }) |
      ranges::views::take(m_frame_pools.max_size()) |
      ranges::to<decltype(m_frame_pools)>;
}

CommandBuffer CommandAllocator::allocate() {
  auto cmd_buffer = m_frame_pools[m_frame_index].allocate();
  return {m_device, cmd_buffer};
}

void CommandAllocator::begin_frame() {
  m_frame_index = (m_frame_index + 1) % m_frame_pools.size();
  m_frame_pools[m_frame_index].reset();
}

void CommandAllocator::end_frame() {}

} // namespace ren
