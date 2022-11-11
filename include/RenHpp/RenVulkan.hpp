#pragma once
#include "Ren.hpp"
#include "Ren/RenVulkan.h"

#include <span>

namespace Ren::Vk {
inline uint32_t getRequiredAPIVersion() {
  return Ren_Vk_GetRequiredAPIVersion();
}

inline std::span<const char *const> getRequiredLayers() {
  return {Ren_Vk_GetRequiredLayers(), Ren_Vk_GetNumRequiredLayers()};
}

inline std::span<const char *const> getRequiredExtensions() {
  return {Ren_Vk_GetRequiredExtensions(), Ren_Vk_GetNumRequiredExtensions()};
}

inline Device createDevice(PFN_vkGetInstanceProcAddr proc, VkInstance instance,
                           VkPhysicalDevice physical_device) {
  return Device(Ren_Vk_CreateDevice(proc, instance, physical_device));
}
} // namespace Ren::Vk
