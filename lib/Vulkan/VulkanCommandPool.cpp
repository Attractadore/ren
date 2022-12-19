#include "Vulkan/VulkanCommandPool.hpp"
#include "Support/Errors.hpp"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanDeviceHandle.inl"

namespace ren {
VulkanCommandPool::VulkanCommandPool(VulkanDevice &device) {
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = device.getGraphicsQueueFamily(),
  };

  VkCommandPool pool;
  throwIfFailed(device.CreateCommandPool(&pool_info, &pool),
                "Vulkan: Failed to create command pool");
  m_pool = {pool, device};
}

VulkanCommandPool::VulkanCommandPool(VulkanCommandPool &&other) = default;

VulkanCommandPool &VulkanCommandPool::operator=(VulkanCommandPool &&other) {
  destroy();
  m_pool = std::move(other.m_pool);
  m_cmd_buffers = std::move(other.m_cmd_buffers);
  return *this;
}

void VulkanCommandPool::destroy() {
  if (m_pool) {
    getDevice().pushToDeleteQueue([pool = m_pool.get(),
                                   cmd_buffers = std::move(m_cmd_buffers)](
                                      VulkanDevice &device) {
      device.FreeCommandBuffers(pool, cmd_buffers.size(), cmd_buffers.data());
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
        .commandPool = m_pool.get(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = alloc_count,
    };
    throwIfFailed(getDevice().AllocateCommandBuffers(
                      &alloc_info, m_cmd_buffers.data() + old_capacity),
                  "Vulkan: Failed to allocate command buffers");
  }
  return m_cmd_buffers[m_allocated_count++];
}

void VulkanCommandPool::reset(VulkanCommandPoolResources resources) {
  getDevice().ResetCommandPool(
      m_pool.get(), static_cast<VkCommandPoolResetFlagBits>(resources));
  m_allocated_count = 0;
}
} // namespace ren
