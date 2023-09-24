#pragma once
#include "ren.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef VkResult (*RenPFNCreateSurface)(VkInstance instance, void *user_data,
                                        VkSurfaceKHR *p_surface);

RenResult ren_vk_CreateSwapchain(RenPFNCreateSurface create_surface,
                                 void *usrptr, RenSwapchain **p_swapchain);

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain);

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain);

RenResult ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                         VkPresentModeKHR present_mode);

#ifdef __cplusplus
}
#endif
