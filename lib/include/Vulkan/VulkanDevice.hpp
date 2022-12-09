#pragma once
#include "Device.hpp"
#include "Support/Errors.hpp"
#include "Support/LinearMap.hpp"
#include "VMA.h"
#include "VulkanDispatchTable.hpp"

#include <cassert>
#include <chrono>

namespace ren {
class VulkanSwapchain;

enum class SemaphoreWaitResult {
  Ready,
  Timeout,
};

class VulkanDevice final : public Device,
                           public InstanceFunctionsMixin<VulkanDevice>,
                           public PhysicalDeviceFunctionsMixin<VulkanDevice>,
                           public DeviceFunctionsMixin<VulkanDevice> {
  VkInstance m_instance = VK_NULL_HANDLE;
  VkPhysicalDevice m_adapter = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VmaAllocator m_allocator = VK_NULL_HANDLE;
  unsigned m_graphics_queue_family = -1;
  VkQueue m_graphics_queue = VK_NULL_HANDLE;
  VulkanDispatchTable m_vk = {};

  HashMap<VkImage, SmallLinearMap<VkImageViewCreateInfo, VkImageView, 3>>
      m_image_views;

private:
  VkImageView getVkImageViewImpl(VkImage image,
                                 const VkImageViewCreateInfo &view_info);
  void destroyImageViews(VkImage image);

public:
  VulkanDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
               VkPhysicalDevice m_adapter);
  ~VulkanDevice();

  static uint32_t getRequiredAPIVersion() { return VK_API_VERSION_1_3; }
  static std::span<const char *const> getRequiredLayers();
  static std::span<const char *const> getRequiredExtensions();

  const VulkanDispatchTable &getDispatchTable() const { return m_vk; }

  VkInstance getInstance() const { return m_instance; }

  VkPhysicalDevice getPhysicalDevice() const { return m_adapter; }

  VkDevice getDevice() const { return m_device; }

  const VkAllocationCallbacks *getAllocator() const { return nullptr; }

  Texture createTexture(const TextureDesc &desc) override;

  VkImageView getVkImageView(const RenderTargetView &rtv);
  VkImageView getVkImageView(const DepthStencilView &dsv);

  VkSemaphore createBinarySemaphore();
  VkSemaphore createTimelineSemaphore(uint64_t initial_value = 0);
  SemaphoreWaitResult waitForSemaphore(VkSemaphore sem, uint64_t value,
                                       std::chrono::nanoseconds timeout);
  void waitForSemaphore(VkSemaphore sem, uint64_t value) {
    auto r = waitForSemaphore(sem, value, std::chrono::nanoseconds::max());
    assert(r == SemaphoreWaitResult::Ready);
  }

  VkQueue getGraphicsQueue() { return m_graphics_queue; }
  unsigned getGraphicsQueueFamily() const { return m_graphics_queue_family; }
  void graphicsQueueSubmit(std::span<const VkSubmitInfo2> submits) {
    throwIfFailed(QueueSubmit2(getGraphicsQueue(), submits.size(),
                               submits.data(), VK_NULL_HANDLE),
                  "Vulkan: Failed to submit work to graphics queue");
  }
  void graphicsQueueWaitIdle() {
    throwIfFailed(QueueWaitIdle(getGraphicsQueue()),
                  "Vulkan: Failed to wait for idle graphics queue");
  }

  VkResult queuePresent(const VkPresentInfoKHR &present_info) {
    return QueuePresentKHR(getGraphicsQueue(), &present_info);
  }

  std::unique_ptr<RenderGraph::Builder> createRenderGraphBuilder() override;
  std::unique_ptr<CommandAllocator>
  createCommandBufferPool(unsigned pipeline_depth) override;

  SyncObject createSyncObject(const SyncDesc &desc) override;

  std::unique_ptr<VulkanSwapchain> createSwapchain(VkSurfaceKHR surface);
};
} // namespace ren
