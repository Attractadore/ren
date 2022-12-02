#include "ren/ren-vk.h"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include <cassert>

extern "C" {
uint32_t ren_vk_GetRequiredAPIVersion() {
  return ren::VulkanDevice::getRequiredAPIVersion();
}

size_t ren_vk_GetRequiredLayerCount() {
  return ren::VulkanDevice::getRequiredLayers().size();
}

const char *const *ren_vk_GetRequiredLayers() {
  return ren::VulkanDevice::getRequiredLayers().data();
}

size_t ren_vk_GetRequiredExtensionCount() {
  return ren::VulkanDevice::getRequiredExtensions().size();
}

const char *const *ren_vk_GetRequiredExtensions() {
  return ren::VulkanDevice::getRequiredExtensions().data();
}

RenDevice *ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance,
                               VkPhysicalDevice m_adapter) {
  return new ren::VulkanDevice(proc, instance, m_adapter);
}

RenSwapchain *ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface) {
  assert(device);
  assert(surface);
  auto *vk_device = static_cast<ren::VulkanDevice *>(device);
  return vk_device->createSwapchain(surface).release();
}
}
