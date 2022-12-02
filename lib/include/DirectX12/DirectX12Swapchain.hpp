#pragma once
#include "Support/ComPtr.hpp"
#include "Swapchain.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>

namespace ren {
class DirectX12Swapchain final : public Swapchain {
  ComPtr<IDXGISwapChain3> m_swapchain;

public:
  DirectX12Swapchain(IDXGIFactory2 *factory, ID3D12CommandQueue *queue,
                     HWND hwnd);

  void setSize(unsigned width, unsigned height) override;

  HWND getHWND() const;
};
} // namespace ren
