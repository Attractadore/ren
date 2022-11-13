#pragma once
#include "ren-vk.h"
#include "ren.hpp"

#include <span>

namespace ren::vk {
inline namespace v0 {
inline uint32_t getRequiredAPIVersion() {
  return ren_vk_GetRequiredAPIVersion();
}

inline std::span<const char *const> getRequiredLayers() {
  return {ren_vk_GetRequiredLayers(), ren_vk_GetRequiredLayerCount()};
}

inline std::span<const char *const> getRequiredExtensions() {
  return {ren_vk_GetRequiredExtensions(), ren_vk_GetRequiredExtensionCount()};
}

struct Swapchain : ::ren::Swapchain {
  auto getSurface() const { return ren_vk_GetSwapchainSurface(this); }

  void setPresentMode(VkPresentModeKHR present_mode) {
    ren_vk_SetSwapchainPresentMode(this, present_mode);
  };

  auto getPresentMode() const { return ren_vk_GetSwapchainPresentMode(this); }
};
using UniqueSwapchain = std::unique_ptr<Swapchain, SwapchainDeleter>;
using SharedSwapchain = std::shared_ptr<Swapchain>;

struct Device : ::ren::Device {
#ifndef VK_NO_PROTOTYPE
  static auto create(VkInstance instance, VkPhysicalDevice adapter) {
    return create(vkGetInstanceProcAddr, instance, adapter);
  }
#endif

  using Unique = std::unique_ptr<Device, DeviceDeleter>;
  static Unique create(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
                       VkPhysicalDevice adapter) {
    return {static_cast<Device *>(ren_vk_CreateDevice(proc, instance, adapter)),
            DeviceDeleter()};
  }

  UniqueSwapchain createSwapchain(VkSurfaceKHR surface) {
    return {static_cast<Swapchain *>(ren_vk_CreateSwapchain(this, surface)),
            SwapchainDeleter()};
  }
};
using UniqueDevice = Device::Unique;
using SharedDevice = std::shared_ptr<Device>;
} // namespace v0
} // namespace ren::vk
