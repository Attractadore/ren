#pragma once
#include "ren.hpp"

#include <vulkan/vulkan.h>

namespace ren::vk {

using PFNCreateSurface = auto (*)(VkInstance instance, void *usrptr,
                                  VkSurfaceKHR *out) -> VkResult;

struct Swapchain : ren::Swapchain {
  static auto create(PFNCreateSurface create_surface, void *usrptr)
      -> expected<std::unique_ptr<ren::Swapchain>>;

protected:
  Swapchain() = default;
};

} // namespace ren::vk
