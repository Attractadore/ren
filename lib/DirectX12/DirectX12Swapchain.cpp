#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/DXGIFormat.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"

namespace ren {
DirectX12Swapchain::DirectX12Swapchain(IDXGIFactory2 *factory,
                                       ID3D12CommandQueue *queue, HWND hwnd) {
  m_hwnd = hwnd;
  DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .BufferCount = c_buffer_count,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  ComPtr<IDXGISwapChain1> swapchain;
  throwIfFailed(factory->CreateSwapChainForHwnd(queue, hwnd, &swapchain_desc,
                                                nullptr, nullptr, &swapchain),
                "DXGI: Failed to create swapchain");
  throwIfFailed(swapchain.As(&m_swapchain), "DXGI: Failed to create swapchain");
  m_textures.resize(c_buffer_count);
  setTextures();
}

void DirectX12Swapchain::setTextures() {
  for (int i = 0; i < c_buffer_count; ++i) {
    ComPtr<ID3D12Resource> surface;
    m_swapchain->GetBuffer(i, IID_PPV_ARGS(&surface));
    auto desc = surface->GetDesc();
    m_textures[i] = {
        .desc =
            {
                .type = TextureType::e2D,
                .format = getFormat(desc.Format),
                .usage = getTextureUsageFlags(desc.Flags),
                .width = static_cast<unsigned>(desc.Width),
                .height = desc.Height,
                .layers = desc.DepthOrArraySize,
                .levels = desc.MipLevels,
            },
        .handle = AnyRef(surface.Get(), [](void *) {}),
    };
  }
}

namespace {
std::tuple<unsigned, unsigned> getWindowSize(HWND hwnd) {
  RECT rect;
  throwIfFailed(!!GetClientRect(hwnd, &rect),
                "WIN32: Failed to get window size");
  return {rect.right, rect.bottom};
}

std::tuple<unsigned, unsigned> getSwapchainSize(IDXGISwapChain1 *swapchain) {
  DXGI_SWAP_CHAIN_DESC1 desc;
  throwIfFailed(swapchain->GetDesc1(&desc),
                "DXGI: Failed to get swapchain description");
  return {desc.Width, desc.Height};
}
} // namespace

void DirectX12Swapchain::AcquireBuffer(DirectX12CommandAllocator &cmd_alloc) {
  // If the swapchain's window is minimized, don't do anything
  if (IsIconic(m_hwnd)) {
    return;
  }
  auto window_size = getWindowSize(m_hwnd);
  auto swapchain_size = getSwapchainSize(m_swapchain.Get());
  if (window_size != swapchain_size) {
    // All accesses to the swapchain's buffers must be completed and all
    // references to them must be released.
    cmd_alloc.flush();
    throwIfFailed(m_swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0),
                  "DXGI: Failed to resize swapchain");
    setTextures();
    // NOTE: swapchain size might still not match window size
  }
}

void DirectX12Swapchain::PresentBuffer() {
  throwIfFailed(m_swapchain->Present(1, 0),
                "DXGI: Failed to present swapchain buffer");
}
} // namespace ren
