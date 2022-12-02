#include "ren/ren-dx12.h"
#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"

using namespace ren;

extern "C" {
Device *ren_dx12_CreateDevice(LUID adapter) {
  return new DirectX12Device(adapter);
}

Swapchain *ren_dx12_CreateSwapchain(Device *device, HWND hwnd) {
  auto *dx12_device = static_cast<DirectX12Device *>(device);
  return dx12_device->createSwapchain(hwnd).release();
}

HWND ren_dx12_GetSwapchainHWND(const Swapchain *swapchain) {
  auto *dx12_swapchain = static_cast<const DirectX12Swapchain *>(swapchain);
  return dx12_swapchain->getHWND();
}
}
