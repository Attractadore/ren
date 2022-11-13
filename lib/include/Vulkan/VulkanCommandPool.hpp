#pragma once
#include "Support/Vector.hpp"

#include <vulkan/vulkan.h>

namespace ren {
class VulkanDevice;

enum class VulkanCommandPoolResources {
  Keep = 0,
  Release = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT,
};

class VulkanCommandPool {
  VulkanDevice *m_device;
  VkCommandPool m_pool;
  Vector<VkCommandBuffer> m_cmd_buffers;
  unsigned m_allocated_count = 0;

private:
  void destroy();

public:
  VulkanCommandPool(VulkanDevice *device);
  VulkanCommandPool(const VulkanCommandPool &) = delete;
  VulkanCommandPool(VulkanCommandPool &&other);
  VulkanCommandPool &operator=(const VulkanCommandPool &) = delete;
  VulkanCommandPool &operator=(VulkanCommandPool &&other);
  ~VulkanCommandPool();

  VkCommandBuffer allocateCommandBuffer();
  void reset(
      VulkanCommandPoolResources resources = VulkanCommandPoolResources::Keep);
};
} // namespace ren
