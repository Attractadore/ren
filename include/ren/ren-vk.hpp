#pragma once
#include "ren-vk.h"
#include "ren.hpp"

#include <span>

namespace ren::vk {
inline namespace v0 {
inline auto get_required_api_version() -> uint32_t {
  return ren_vk_GetRequiredAPIVersion();
}

inline auto get_required_layers() -> std::span<const char *const> {
  return {ren_vk_GetRequiredLayers(), ren_vk_GetRequiredLayerCount()};
}

inline auto get_required_extensions() -> std::span<const char *const> {
  return {ren_vk_GetRequiredExtensions(), ren_vk_GetRequiredExtensionCount()};
}

struct Swapchain : ::ren::Swapchain {
  auto get_surface() const -> VkSurfaceKHR {
    return ren_vk_GetSwapchainSurface(this);
  }

  auto get_present_mode() const -> VkPresentModeKHR {
    return ren_vk_GetSwapchainPresentMode(this);
  }

  [[nodiscard]] auto set_present_mode(VkPresentModeKHR present_mode)
      -> expected<void> {
    if (auto err = ren_vk_SetSwapchainPresentMode(this, present_mode)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  };
};

struct Device : ::ren::Device {
#ifndef VK_NO_PROTOTYPE
  static auto create(VkInstance instance, VkPhysicalDevice adapter) {
    return create(vkGetInstanceProcAddr, instance, adapter);
  }
#endif

  static auto create(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
                     VkPhysicalDevice adapter) -> expected<UniqueDevice> {
    RenDevice *device = nullptr;
    if (auto err = ren_vk_CreateDevice(proc, instance, adapter, &device)) {
      return unexpected(static_cast<Error>(err));
    }
    return UniqueDevice(static_cast<Device *>(device), DeviceDeleter());
  }

  auto create_swapchain(VkSurfaceKHR surface) -> expected<UniqueSwapchain> {
    RenSwapchain *swapchain = nullptr;
    if (auto err = ren_vk_CreateSwapchain(this, surface, &swapchain)) {
      return unexpected(static_cast<Error>(err));
    }
    return UniqueSwapchain(static_cast<Swapchain *>(swapchain),
                           SwapchainDeleter());
  }
};
} // namespace v0
} // namespace ren::vk
