#pragma once
#include "ren/ren-dx12.h"
#include "ren/ren.hpp"

namespace ren::dx12 {
inline namespace v0 {
struct Swapchain : ::ren::Swapchain {};
using UniqueSwapchain = std::unique_ptr<Swapchain, SwapchainDeleter>;
using SharedSwapchain = std::shared_ptr<Swapchain>;

struct Device : ::ren::Device {
  using Unique = std::unique_ptr<Device, DeviceDeleter>;
  static Unique create(LUID adapter) {
    return {static_cast<Device *>(ren_dx12_CreateDevice(adapter)),
            DeviceDeleter()};
  }

  UniqueSwapchain createSwapchain(HWND hwnd) {
    return {static_cast<Swapchain *>(ren_dx12_CreateSwapchain(this, hwnd)),
            SwapchainDeleter()};
  }
};
using UniqueDevice = Device::Unique;
using SharedDevice = std::shared_ptr<Device>;
} // namespace v0
} // namespace ren::dx12
