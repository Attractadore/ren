#include "Ren/RenVulkan.h"
#include "Support/Array.hpp"
#include "Vulkan/VulkanDevice.hpp"

namespace Ren {
namespace {
constexpr auto layers = makeArray<const char *>(
#ifndef NDEBUG
    "VK_LAYER_KHRONOS_validation"
#endif
);

constexpr auto extensions = makeArray<const char *>();
} // namespace
} // namespace Ren

extern "C" {
uint32_t Ren_Vk_GetRequiredAPIVersion() { return VK_API_VERSION_1_3; }

size_t Ren_Vk_GetNumRequiredLayers() { return Ren::layers.size(); }

const char *const *Ren_Vk_GetRequiredLayers() { return Ren::layers.data(); }

size_t Ren_Vk_GetNumRequiredExtensions() { return Ren::extensions.size(); }

const char *const *Ren_Vk_GetRequiredExtensions() {
  return Ren::extensions.data();
}

RenDevice *Ren_Vk_CreateDevice(PFN_vkGetInstanceProcAddr proc,
                               VkInstance instance,
                               VkPhysicalDevice physical_device) {
  return new Ren::VulkanDevice(proc, instance, physical_device);
}
}
