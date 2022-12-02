#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12Swapchain::DirectX12Swapchain(IDXGIFactory2 *factory,
                                       ID3D12CommandQueue *queue, HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .BufferCount = 3,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  ComPtr<IDXGISwapChain1> swapchain;
  throwIfFailed(factory->CreateSwapChainForHwnd(queue, hwnd, &swapchain_desc,
                                                nullptr, nullptr, &swapchain),
                "DXGI: Failed to create swapchain");
  throwIfFailed(swapchain.As(&m_swapchain), "DXGI: Failed to create swapchain");
} // namespace ren

void DirectX12Swapchain::setSize(unsigned width, unsigned height) {}

HWND DirectX12Swapchain::getHWND() const { DIRECTX12_UNIMPLEMENTED; }
} // namespace ren
