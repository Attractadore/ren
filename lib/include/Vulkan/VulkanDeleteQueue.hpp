#pragma once
#include "DeleteQueue.hpp"
#include "VMA.h"

#include <cstdint>

namespace ren {
class VulkanDevice;

template <typename T> using VulkanQueueDeleter = QueueDeleter<VulkanDevice, T>;
using VulkanQueueCustomDeleter = QueueCustomDeleter<VulkanDevice>;

struct VMABuffer {
  VkBuffer buffer;
  VmaAllocation allocation;
};

struct VMAImage {
  VkImage image;
  VmaAllocation allocation;
};

struct SwapchainImage {
  VkImage image;
};

using VulkanDeleteQueue =
    DeleteQueue<VulkanDevice, VulkanQueueCustomDeleter, VMABuffer, VMAImage,
                VkSemaphore, VkSwapchainKHR, SwapchainImage>;
} // namespace ren
