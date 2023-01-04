#pragma once
#include "VulkanDeleteQueue.hpp"
#include "VulkanDevice.hpp"

namespace ren {
#define define_vulkan_queue_deleter(T, F)                                      \
  template <> struct QueueDeleter<VulkanDevice, T> {                           \
    void operator()(VulkanDevice &device, T handle) const noexcept {           \
      device.F(handle);                                                        \
    }                                                                          \
  }

template <> struct QueueDeleter<VulkanDevice, VulkanImageViews> {
  void operator()(VulkanDevice &device, VulkanImageViews views) const noexcept {
    device.destroyImageViews(views.image);
  }
};

template <> struct QueueDeleter<VulkanDevice, VmaAllocation> {
  void operator()(VulkanDevice &device,
                  VmaAllocation allocation) const noexcept {
    vmaFreeMemory(device.getVMAAllocator(), allocation);
  }
};

define_vulkan_queue_deleter(VkBuffer, DestroyBuffer);
define_vulkan_queue_deleter(VkImage, DestroyImage);
define_vulkan_queue_deleter(VkPipeline, DestroyPipeline);
define_vulkan_queue_deleter(VkSemaphore, DestroySemaphore);
define_vulkan_queue_deleter(VkSwapchainKHR, DestroySwapchainKHR);

#undef define_vulkan_queue_deleter
} // namespace ren
