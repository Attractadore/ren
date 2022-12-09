#pragma once
#include "Support/ComPtr.hpp"
#include "Support/Vector.hpp"
#include "Swapchain.hpp"
#include "Texture.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>

namespace ren {
class DirectX12CommandAllocator;
class DirectX12Device;

class DirectX12Swapchain final : public Swapchain {
  static constexpr auto c_buffer_count = 3;

  DirectX12Device *m_device;
  HWND m_hwnd;
  ComPtr<IDXGISwapChain3> m_swapchain;
  SmallVector<Texture, 3> m_textures;

private:
  void setTextures();

public:
  DirectX12Swapchain(DirectX12Device *device, HWND hwnd);

  void setSize(unsigned width, unsigned height) override {}

  HWND getHWND() const { return m_hwnd; }

  void AcquireBuffer(DirectX12CommandAllocator &cmd_alloc);
  const Texture &getTexture() {
    return m_textures[m_swapchain->GetCurrentBackBufferIndex()];
  }
  void PresentBuffer();
};
} // namespace ren
