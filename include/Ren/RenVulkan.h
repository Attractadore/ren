#pragma once
#include "Ren.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t Ren_Vk_GetRequiredAPIVersion();
size_t Ren_Vk_GetNumRequiredLayers();
const char *const *Ren_Vk_GetRequiredLayers();
size_t Ren_Vk_GetNumRequiredExtensions();
const char *const *Ren_Vk_GetRequiredExtensions();

RenDevice *Ren_Vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance,
                               VkPhysicalDevice physical_device);

#ifdef __cplusplus
}
#endif
