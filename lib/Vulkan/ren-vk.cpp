#include "ren/ren-vk.h"
#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include <cassert>

using namespace ren;

extern "C" {
uint32_t ren_vk_GetRequiredAPIVersion() {
  return VulkanDevice::getRequiredAPIVersion();
}

size_t ren_vk_GetRequiredLayerCount() {
  return VulkanDevice::getRequiredLayers().size();
}

const char *const *ren_vk_GetRequiredLayers() {
  return VulkanDevice::getRequiredLayers().data();
}

size_t ren_vk_GetRequiredExtensionCount() {
  return VulkanDevice::getRequiredExtensions().size();
}

const char *const *ren_vk_GetRequiredExtensions() {
  return VulkanDevice::getRequiredExtensions().data();
}

RenDevice *ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance,
                               VkPhysicalDevice m_adapter) {
  return new VulkanDevice(proc, instance, m_adapter);
}

RenSwapchain *ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface) {
  assert(device);
  assert(surface);
  auto *vk_device = static_cast<VulkanDevice *>(device);
  return vk_device->createSwapchain(surface).release();
}

VkSurfaceKHR ren_vk_GetSwapchainSurface(const Swapchain *swapchain) {
  assert(swapchain);
  auto *vk_swapchain = static_cast<const VulkanSwapchain *>(swapchain);
  return vk_swapchain->get_surface();
}

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const Swapchain *swapchain) {
  assert(swapchain);
  auto *vk_swapchain = static_cast<const VulkanSwapchain *>(swapchain);
  return vk_swapchain->get_present_mode();
}

void ren_vk_SetSwapchainPresentMode(Swapchain *swapchain,
                                    VkPresentModeKHR present_mode) {
  assert(swapchain);
  auto *vk_swapchain = static_cast<VulkanSwapchain *>(swapchain);
  vk_swapchain->set_present_mode(present_mode);
}
}
