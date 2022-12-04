#pragma once
#include "D3D12MA.hpp"
#include "Device.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12.h>
#include <dxgi1_2.h>

namespace ren {
class DirectX12Swapchain;

class DirectX12Device final : public Device {
  ComPtr<IDXGIFactory2> m_factory;
  ComPtr<IDXGIAdapter1> m_adapter;
  ComPtr<ID3D12Device> m_device;
  ComPtr<D3D12MA::Allocator> m_allocator;
  ComPtr<ID3D12CommandQueue> m_graphics_queue;

public:
  DirectX12Device(LUID adapter);

  std::unique_ptr<DirectX12Swapchain> createSwapchain(HWND hwnd);

  std::unique_ptr<RenderGraph::Builder> createRenderGraphBuilder() override;
  std::unique_ptr<ren::CommandAllocator>
  createCommandBufferPool(unsigned pipeline_depth) override;

  Texture createTexture(const ren::TextureDesc &desc) override;

  SyncObject createSyncObject(const ren::SyncDesc &desc) override;
};
} // namespace ren
