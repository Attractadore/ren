#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/Errors.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12sdklayers.h>
#include <dxgi1_3.h>

namespace ren {
namespace {
constexpr UINT factory_flags =
#if REN_DIRECTX12_DEBUG
    DXGI_CREATE_FACTORY_DEBUG |
#endif
    0;
} // namespace

DirectX12Device::DirectX12Device(LUID adapter) {
  throwIfFailed(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&m_factory)),
                "DXGI: Failed to create factory");

#if REN_DIRECTX12_DEBUG
  {
    ComPtr<ID3D12Debug> debug_controller;
    throwIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)),
                  "DirectX 12: Failed to get debug controller");
    debug_controller->EnableDebugLayer();
  }
#endif

  for (int i = 0;; ++i) {
    throwIfFailed(m_factory->EnumAdapters1(i, &m_adapter),
                  "DXGI: Failed to find adapter");
    DXGI_ADAPTER_DESC1 desc;
    throwIfFailed(m_adapter->GetDesc1(&desc),
                  "DXGI: Failed to get adapter description");
    if (desc.AdapterLuid.HighPart == adapter.HighPart and
        desc.AdapterLuid.LowPart == adapter.LowPart) {
      break;
    }
  }

  throwIfFailed(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                  IID_PPV_ARGS(&m_device)),
                "DirectX 12: Failed to create device");

  D3D12_COMMAND_QUEUE_DESC queue_desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
  };

  throwIfFailed(m_device->CreateCommandQueue(&queue_desc,
                                             IID_PPV_ARGS(&m_graphics_queue)),
                "DirectX 12: Failed to create graphics queue");
}

std::unique_ptr<DirectX12Swapchain>
DirectX12Device::createSwapchain(HWND hwnd) {
  return std::make_unique<DirectX12Swapchain>(m_factory.Get(),
                                              m_graphics_queue.Get(), hwnd);
}

std::unique_ptr<RenderGraph::Builder>
DirectX12Device::createRenderGraphBuilder() {
  DIRECTX12_UNIMPLEMENTED;
}

std::unique_ptr<ren::CommandAllocator>
DirectX12Device::createCommandBufferPool(unsigned pipeline_depth) {
  DIRECTX12_UNIMPLEMENTED;
}

Texture DirectX12Device::createTexture(const ren::TextureDesc &desc) {
  DIRECTX12_UNIMPLEMENTED;
}

SyncObject DirectX12Device::createSyncObject(const ren::SyncDesc &desc) {
  DIRECTX12_UNIMPLEMENTED;
}
} // namespace ren
