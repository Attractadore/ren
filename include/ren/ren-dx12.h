#pragma once
#include "ren.h"

#include <d3d12.h>

#ifdef __cplusplus
extern "C" {
#endif

RenDevice *ren_dx12_CreateDevice(LUID adapter);

RenSwapchain *ren_dx12_CreateSwapchain(RenDevice *device, HWND hwnd);

HWND ren_dx12_GetSwapchainHWND(const RenSwapchain *swapchain);

#ifdef __cplusplus
}
#endif
