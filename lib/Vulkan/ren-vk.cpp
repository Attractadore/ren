#include "ren/ren-vk.h"
#include "Support/Array.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include <cassert>

namespace ren {
namespace {
constexpr auto layers = makeArray<const char *>(
#ifndef NDEBUG
    "VK_LAYER_KHRONOS_validation"
#endif
);

constexpr auto extensions = makeArray<const char *>();
} // namespace
} // namespace ren

extern "C" {
uint32_t ren_vk_GetRequiredAPIVersion() {
  return ren::VulkanDevice::getRequiredAPIVersion();
}

size_t ren_vk_GetRequiredLayerCount() { return ren::layers.size(); }

const char *const *ren_vk_GetRequiredLayers() { return ren::layers.data(); }

size_t ren_vk_GetRequiredExtensionCount() { return ren::extensions.size(); }

const char *const *ren_vk_GetRequiredExtensions() {
  return ren::extensions.data();
}

RenDevice *ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance,
                               VkPhysicalDevice m_adapter) {
  return new ren::VulkanDevice(proc, instance, m_adapter);
}

RenSwapchain *ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface) {
  assert(device);
  assert(surface);
  return new ren::VulkanSwapchain(static_cast<ren::VulkanDevice *>(device),
                                  surface);
}
}
