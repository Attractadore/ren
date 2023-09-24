#pragma once
#include "ren-vk.h"
#include "ren.hpp"

#include <functional>
#include <span>

namespace ren::vk {
inline namespace v0 {

using PFN_CreateSurface = RenPFNCreateSurface;

struct Swapchain : ::ren::Swapchain {
  static auto create(PFN_CreateSurface create_surface, void *usrptr)
      -> expected<UniqueSwapchain> {
    RenSwapchain *swapchain;
    return detail::make_expected(
               ren_vk_CreateSwapchain(create_surface, usrptr, &swapchain))
        .transform([&] {
          return UniqueSwapchain(static_cast<Swapchain *>(swapchain));
        });
  }

  auto get_surface() const -> VkSurfaceKHR {
    return ren_vk_GetSwapchainSurface(this);
  }

  auto get_present_mode() const -> VkPresentModeKHR {
    return ren_vk_GetSwapchainPresentMode(this);
  }

  [[nodiscard]] auto set_present_mode(VkPresentModeKHR present_mode)
      -> expected<void> {
    if (auto err = ren_vk_SetSwapchainPresentMode(this, present_mode)) {
      return unexpected(static_cast<Error>(err));
    }
    return {};
  };
};

} // namespace v0
} // namespace ren::vk
