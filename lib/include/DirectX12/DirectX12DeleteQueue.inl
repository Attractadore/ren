#pragma once
#include "DirectX12DeleteQueue.hpp"
#include "DirectX12Device.hpp"

namespace ren {
template <> struct QueueDeleter<DirectX12Device, IUnknown *> {
  void operator()(DirectX12Device &, IUnknown *handle) const noexcept {
    handle->Release();
  }
};

template <> struct QueueDeleter<DirectX12Device, DirectX12TextureViews> {
  void operator()(DirectX12Device &device,
                  DirectX12TextureViews views) const noexcept {
    device.destroyTextureViews(views.resource);
  }
};
} // namespace ren
