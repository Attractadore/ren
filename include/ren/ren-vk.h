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

RenDevice *ren_vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance, VkPhysicalDevice adapter);

RenSwapchain *ren_vk_CreateSwapchain(RenDevice *device, VkSurfaceKHR surface);

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain);

void ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                    VkPresentModeKHR present_mode);
VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain);

#ifdef __cplusplus
}
#endif
