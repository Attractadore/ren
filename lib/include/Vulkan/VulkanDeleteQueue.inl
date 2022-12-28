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

template <> struct QueueDeleter<VulkanDevice, VMABuffer> {
  void operator()(VulkanDevice &device, VMABuffer vma_buffer) const noexcept {
    device.destroyBufferWithAllocation(vma_buffer.buffer, vma_buffer.allocation);
  }
};

template <> struct QueueDeleter<VulkanDevice, VMAImage> {
  void operator()(VulkanDevice &device, VMAImage vma_image) const noexcept {
    device.destroyImageWithAllocation(vma_image.image, vma_image.allocation);
  }
};

template <> struct QueueDeleter<VulkanDevice, SwapchainImage> {
  void operator()(VulkanDevice &device, SwapchainImage image) const noexcept {
    device.destroyImageViews(image.image);
  }
};

define_vulkan_queue_deleter(VkSemaphore, DestroySemaphore);
define_vulkan_queue_deleter(VkSwapchainKHR, DestroySwapchainKHR);

#undef define_vulkan_queue_deleter
} // namespace ren
