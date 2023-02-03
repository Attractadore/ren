#pragma once
#include "ren.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  PFN_vkGetInstanceProcAddr proc;
  uint32_t num_instance_extensions;
  const char *const *instance_extensions;
  uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
} RenDeviceDesc;

RenResult ren_vk_CreateDevice(const RenDeviceDesc *desc, RenDevice **p_device);

typedef VkResult (*RenPFNCreateSurface)(VkInstance instance, void *user_data,
                                        VkSurfaceKHR *p_surface);

RenResult ren_vk_CreateSwapchain(RenDevice *device,
                                 RenPFNCreateSurface create_surface,
                                 void *usrptr, RenSwapchain **p_swapchain);

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain);

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain);

RenResult ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                         VkPresentModeKHR present_mode);

#ifdef __cplusplus
}
#endif
