#pragma once
#include "ren.hpp"

#include <vulkan/vulkan.h>

namespace ren::vk {

using PFNCreateSurface = auto (*)(VkInstance instance, void *usrptr,
                                  VkSurfaceKHR *out) -> VkResult;

[[nodiscard]] auto create_swapchain(PFNCreateSurface create_surface,
                                    void *usrptr) -> expected<Swapchain>;

} // namespace ren::vk
