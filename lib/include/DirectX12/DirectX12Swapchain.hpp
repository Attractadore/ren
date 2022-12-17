#pragma once
#include "DeviceHandle.hpp"
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
  DeviceHandle<IDXGISwapChain3> m_swapchain;
  SmallVector<Texture, 3> m_textures;
  DeviceHandle<ID3D12RootSignature> m_blit_root_sig;
  DeviceHandle<ID3D12PipelineState> m_blit_pso;

private:
  void setTextures();

public:
  DirectX12Swapchain(DirectX12Device *device, HWND hwnd);
  DirectX12Swapchain(const DirectX12Swapchain &) = delete;
  DirectX12Swapchain(DirectX12Swapchain &&);
  DirectX12Swapchain &operator=(const DirectX12Swapchain &) = delete;
  DirectX12Swapchain &operator=(DirectX12Swapchain &&);
  ~DirectX12Swapchain();

  void setSize(unsigned width, unsigned height) override {}

  HWND getHWND() const { return m_hwnd; }

  void AcquireBuffer();
  const Texture &getTexture() {
    return m_textures[m_swapchain->GetCurrentBackBufferIndex()];
  }
  ID3D12RootSignature *getBlitRootSignature() const {
    return m_blit_root_sig.get();
  }
  ID3D12PipelineState *getBlitPSO() const { return m_blit_pso.get(); }
  void PresentBuffer();
};
} // namespace ren
