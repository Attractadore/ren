#pragma once
#include "ren-vk.h"
#include "ren.hpp"

#include <span>

namespace ren::vk {
inline namespace v0 {

struct Swapchain : ::ren::Swapchain {
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

struct DeviceDesc {
  PFN_vkGetInstanceProcAddr proc;
  std::span<const char *const> instance_extensions;
  uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
};

using PFN_CreateSurface = RenPFNCreateSurface;
using CreateSurfaceFunction =
    std::function<VkResult(VkInstance, VkSurfaceKHR *)>;

struct Device : ::ren::Device {
  static auto create(const DeviceDesc &desc) -> expected<UniqueDevice> {
    assert(desc.proc);
    RenDeviceDesc c_desc = {
        .proc = desc.proc,
        .num_instance_extensions = unsigned(desc.instance_extensions.size()),
        .instance_extensions = desc.instance_extensions.data(),
    };
    std::memcpy(c_desc.pipeline_cache_uuid, desc.pipeline_cache_uuid,
                sizeof(desc.pipeline_cache_uuid));
    RenDevice *device;
    return expected(ren_vk_CreateDevice(&c_desc, &device)).map([&] {
      return UniqueDevice(static_cast<Device *>(device));
    });
  }

  auto create_swapchain(PFN_CreateSurface create_surface, void *usrptr)
      -> expected<UniqueSwapchain> {
    RenSwapchain *swapchain;
    return expected(
               ren_vk_CreateSwapchain(this, create_surface, usrptr, &swapchain))
        .map([&] {
          return UniqueSwapchain(static_cast<Swapchain *>(swapchain));
        });
  }

  auto create_swapchain(CreateSurfaceFunction create_surface)
      -> expected<UniqueSwapchain> {
    struct Helper {
      static VkResult call_function(VkInstance instance, void *function,
                                    VkSurfaceKHR *p_surface) {
        return (*reinterpret_cast<CreateSurfaceFunction *>(function))(
            instance, p_surface);
      }
    };
    return create_swapchain(Helper::call_function, &create_surface);
  }
};

} // namespace v0
} // namespace ren::vk
