#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DXGIFormat.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12RenderGraph.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"

#include <d3d12sdklayers.h>
#include <dxgi1_3.h>

namespace ren {
DirectX12Device::DirectX12Device(LUID adapter) {
  constexpr UINT factory_flags =
#if REN_DIRECTX12_DEBUG
      DXGI_CREATE_FACTORY_DEBUG |
#endif
      0;

  throwIfFailed(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&m_factory)),
                "DXGI: Failed to create factory");

#if REN_DIRECTX12_DEBUG
  {
    ComPtr<ID3D12Debug> debug_controller;
    throwIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)),
                  "D3D12: Failed to get debug controller");
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
                "D3D12: Failed to create device");

  D3D12MA::ALLOCATOR_DESC allocator_desc = {
      .pDevice = m_device.Get(),
      .pAdapter = m_adapter.Get(),
  };

  D3D12MA::Allocator *allocator;
  throwIfFailed(D3D12MA::CreateAllocator(&allocator_desc, &allocator),
                "D3D12MA: Failed to create allocator");
  m_allocator = allocator;

  D3D12_COMMAND_QUEUE_DESC queue_desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
  };

  throwIfFailed(
      m_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_direct_queue)),
      "D3D12: Failed to create graphics queue");
}

std::unique_ptr<DirectX12Swapchain>
DirectX12Device::createSwapchain(HWND hwnd) {
  return std::make_unique<DirectX12Swapchain>(m_factory.Get(),
                                              m_direct_queue.Get(), hwnd);
}

std::unique_ptr<RenderGraph::Builder>
DirectX12Device::createRenderGraphBuilder() {
  return std::make_unique<DirectX12RenderGraph::Builder>(this);
}

std::unique_ptr<ren::CommandAllocator>
DirectX12Device::createCommandBufferPool(unsigned pipeline_depth) {
  return std::make_unique<DirectX12CommandAllocator>(this, pipeline_depth);
}

Texture DirectX12Device::createTexture(const ren::TextureDesc &desc) {
  D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = getD3D12ResourceDimension(desc.type),
      .Width = desc.width,
      .Height = desc.height,
      .DepthOrArraySize = desc.layers,
      .MipLevels = desc.levels,
      .Format = getDXGIFormat(desc.format),
      .SampleDesc = {.Count = 1},
      .Flags = getD3D12ResourceFlags(desc.usage),
  };

  D3D12MA::ALLOCATION_DESC allocation_desc = {.HeapType =
                                                  D3D12_HEAP_TYPE_DEFAULT};

  D3D12MA::Allocation *allocation;
  throwIfFailed(m_allocator->CreateResource(&allocation_desc, &resource_desc,
                                            D3D12_RESOURCE_STATE_COMMON,
                                            nullptr, &allocation, IID_NULL,
                                            nullptr),
                "D3D12MA: Failed to create texture");
  return {
      .desc = desc,
      .handle = AnyRef(allocation->GetResource(),
                       [allocation](void *) { allocation->Release(); }),
  };
}

SyncObject DirectX12Device::createSyncObject(const SyncDesc &desc) {
  DIRECTX12_UNIMPLEMENTED;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Device::getRTV(const TextureView &view) {
  DIRECTX12_UNIMPLEMENTED;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Device::getDSV(const TextureView &view) {
  DIRECTX12_UNIMPLEMENTED;
}
} // namespace ren
