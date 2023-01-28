#pragma once
#include "ren.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t ren_vk_GetRequiredAPIVersion();

size_t ren_vk_GetRequiredLayerCount();
const char *const *ren_vk_GetRequiredLayers();

size_t ren_vk_GetRequiredExtensionCount();
const char *const *ren_vk_GetRequiredExtensions();

RenResult ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                              VkInstance instance, VkPhysicalDevice adapter,
                              RenDevice **p_device);

RenResult ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface,
                                 RenSwapchain **p_swapchain);

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain);

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain);

RenResult ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                         VkPresentModeKHR present_mode);

#ifdef __cplusplus
}
#endif
