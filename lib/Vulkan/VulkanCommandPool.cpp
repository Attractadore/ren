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

VulkanCommandPool::VulkanCommandPool(VulkanCommandPool &&other)
    : m_device(other.m_device),
      m_pool(std::exchange(other.m_pool, VK_NULL_HANDLE)),
      m_cmd_buffers(std::move(other.m_cmd_buffers)),
      m_allocated_count(std::exchange(m_allocated_count, 0)) {}

VulkanCommandPool &VulkanCommandPool::operator=(VulkanCommandPool &&other) {
  destroy();
  m_device = other.m_device;
  m_pool = other.m_pool;
  other.m_pool = VK_NULL_HANDLE;
  m_cmd_buffers = std::move(other.m_cmd_buffers);
  m_allocated_count = other.m_allocated_count;
  other.m_allocated_count = 0;
  return *this;
}

void VulkanCommandPool::destroy() {
  if (m_pool) {
    assert(m_device);
    m_device->push_to_delete_queue([pool = m_pool,
                                    cmd_buffers = std::move(m_cmd_buffers)](
                                       VulkanDevice &device) {
      if (not cmd_buffers.empty()) {
        device.FreeCommandBuffers(pool, cmd_buffers.size(), cmd_buffers.data());
      }
      device.DestroyCommandPool(pool);
    });
  }
}

VulkanCommandPool::~VulkanCommandPool() { destroy(); }

VkCommandBuffer VulkanCommandPool::allocate() {
  [[unlikely]] if (m_allocated_count == m_cmd_buffers.size()) {
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
