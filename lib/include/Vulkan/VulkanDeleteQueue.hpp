#pragma once
#include "DeleteQueue.hpp"
#include "VMA.h"

#include <cstdint>

namespace ren {
class VulkanDevice;

template <typename T> using VulkanQueueDeleter = QueueDeleter<VulkanDevice, T>;
using VulkanQueueCustomDeleter = QueueCustomDeleter<VulkanDevice>;

struct VulkanImageViews {
  VkImage image;
};

using VulkanDeleteQueue =
    DeleteQueue<VulkanDevice, VulkanQueueCustomDeleter, VulkanImageViews,
                VkBuffer, VkImage, VkPipeline, VkPipelineLayout, VkSemaphore,
                VkSwapchainKHR, VmaAllocation>;
} // namespace ren
