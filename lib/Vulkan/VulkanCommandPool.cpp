#include "Vulkan/VulkanCommandPool.hpp"
#include "Support/Errors.hpp"
#include "Vulkan/VulkanDevice.hpp"

namespace ren {
VulkanCommandPool::VulkanCommandPool(VulkanDevice &device) {
  m_device = &device;
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = m_device->getGraphicsQueueFamily(),
  };
  throwIfFailed(m_device->CreateCommandPool(&pool_info, &m_pool),
                "Vulkan: Failed to create command pool");
}

void VulkanCommandPool::destroy() {
  if (m_pool) {
    m_device->push_to_delete_queue([pool = m_pool,
                                    cmd_buffers = std::move(m_cmd_buffers)](
                                       VulkanDevice &device) {
      device.FreeCommandBuffers(pool, cmd_buffers.size(), cmd_buffers.data());
      device.DestroyCommandPool(pool);
    });
  }
}

VulkanCommandPool::~VulkanCommandPool() { destroy(); }

VkCommandBuffer VulkanCommandPool::allocate() {
  if (m_allocated_count == m_cmd_buffers.size()) {
    auto old_capacity = m_cmd_buffers.size();
    auto new_capacity = std::max<size_t>(2 * old_capacity, 1);
    m_cmd_buffers.resize(new_capacity);
    uint32_t alloc_count = new_capacity - old_capacity;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = alloc_count,
    };
    throwIfFailed(m_device->AllocateCommandBuffers(
                      &alloc_info, m_cmd_buffers.data() + old_capacity),
                  "Vulkan: Failed to allocate command buffers");
  }
  return m_cmd_buffers[m_allocated_count++];
}

void VulkanCommandPool::reset(VulkanCommandPoolResources resources) {
  m_device->ResetCommandPool(
      m_pool, static_cast<VkCommandPoolResetFlagBits>(resources));
  m_allocated_count = 0;
}
} // namespace ren
