#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DXGIFormat.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12RenderGraph.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"
#include "Format/Texture.hpp"
#include "Support/Log.hpp"

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

#if REN_DIRECTX12_FORCE_WARP_DEVICE
  throwIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&m_adapter)),
                "DXGI: Failed to find WARP adapter");
#else
  throwIfFailed(m_factory->EnumAdapterByLuid(adapter, IID_PPV_ARGS(&m_adapter)),
                "DXGI: Failed to find adapter");
#endif

  throwIfFailed(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                  IID_PPV_ARGS(&m_device)),
                "D3D12: Failed to create device");

#if REN_DIRECTX12_DEBUG_CALLBACK
  {
    ComPtr<ID3D12InfoQueue1> info_queue;
    throwIfFailed(m_device->QueryInterface(info_queue.GetAddressOf()),
                  "D3D12: Failed to query ID3D12InfoQueue1 interface");
    auto debug_callback = [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY,
                             D3D12_MESSAGE_ID, LPCSTR pDescription,
                             void *) { std::wcerr << pDescription << "\n"; };
    throwIfFailed(
        info_queue->RegisterMessageCallback(
            debug_callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, nullptr),
        "D3D12: Failed to set debug callback");
  }
#endif

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

  m_rtv_pool = std::make_unique<DirectX12CPUDescriptorPool>(
      m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  m_dsv_pool = std::make_unique<DirectX12CPUDescriptorPool>(
      m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  m_cbv_srv_uav_pool = std::make_unique<DirectX12CPUDescriptorPool>(
      m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

std::unique_ptr<DirectX12Swapchain>
DirectX12Device::createSwapchain(HWND hwnd) {
  return std::make_unique<DirectX12Swapchain>(this, hwnd);
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
  auto dxgi_format = getDXGIFormat(desc.format);
  D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = getD3D12ResourceDimension(desc.type),
      .Width = desc.width,
      .Height = desc.height,
      .DepthOrArraySize = desc.layers,
      .MipLevels = desc.levels,
      .Format = dxgi_format,
      .SampleDesc = {.Count = 1},
      .Flags = getD3D12ResourceFlags(desc.usage),
  };

  D3D12MA::ALLOCATION_DESC allocation_desc = {.HeapType =
                                                  D3D12_HEAP_TYPE_DEFAULT};

  D3D12_CLEAR_VALUE clear_value = {.Format = dxgi_format,
                                   .Color = {0.0f, 0.0f, 0.0f, 1.0f}};

  D3D12MA::Allocation *allocation;
  throwIfFailed(m_allocator->CreateResource(
                    &allocation_desc, &resource_desc,
                    D3D12_RESOURCE_STATE_COMMON,
                    [&]() -> const D3D12_CLEAR_VALUE * {
                      if (isColorFormat(desc.format)) {
                        return &clear_value;
                      } else if (isDepthFormat(desc.format) or
                                 isStencilFormat(desc.format)) {
                        clear_value.DepthStencil = {.Depth = 1.0f};
                        return &clear_value;
                      }
                      return nullptr;
                    }(),
                    &allocation, IID_NULL, nullptr),
                "D3D12MA: Failed to create texture");
  return {
      .desc = desc,
      .handle = AnyRef(allocation->GetResource(),
                       [this, allocation](ID3D12Resource *resource) {
                         destroyResourceData(resource);
                         allocation->Release();
                       }),
  };
}

void DirectX12Device::destroyResourceData(ID3D12Resource *resource) {
  destroyTextureViews(resource);
}

void DirectX12Device::destroyResourceRTVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_rtvs[resource]) {
    m_rtv_pool->free({.cpu_handle = desciptor});
  }
  m_rtvs.erase(resource);
}

void DirectX12Device::destroyResourceDSVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_dsvs[resource]) {
    m_dsv_pool->free({.cpu_handle = desciptor});
  }
  m_dsvs.erase(resource);
}

void DirectX12Device::destroyResourceTextureSRVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_texture_srvs[resource]) {
    m_cbv_srv_uav_pool->free(desciptor);
  }
  m_texture_srvs.erase(resource);
}

void DirectX12Device::destroyResourceTextureUAVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_texture_uavs[resource]) {
    m_cbv_srv_uav_pool->free(desciptor);
  }
  m_texture_uavs.erase(resource);
}

void DirectX12Device::destroyTextureViews(ID3D12Resource *resource) {
  destroyResourceRTVs(resource);
  destroyResourceDSVs(resource);
  destroyResourceTextureSRVs(resource);
  destroyResourceTextureUAVs(resource);
}

D3D12_CPU_DESCRIPTOR_HANDLE
DirectX12Device::getRTV(const RenderTargetView &rtv) {
  auto *resource = getD3D12Resource(rtv.texture);
  // TODO: null descriptors
  assert(resource);

  dx12Log("Get RTV for resource {}", fmt::ptr(resource));
  dx12Debug("RTV desc: {}", rtv.desc);

  auto [it, inserted] = m_rtvs[resource].insert(rtv.desc, {});
  auto &handle = std::get<1>(*it);
  if (inserted) {
    dx12Log("No existing RTV found, create a new one");
    handle = m_rtv_pool->allocate().cpu_handle;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
        .Format = getDXGIFormat(getRTVFormat(rtv)),
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray = {
            .MipSlice = rtv.desc.level,
            .FirstArraySlice = rtv.desc.layer,
            .ArraySize = 1,
        }};
    m_device->CreateRenderTargetView(resource, &rtv_desc, handle);
  }
  return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE
DirectX12Device::getDSV(const DepthStencilView &dsv,
                        TargetStoreOp depth_store_op,
                        TargetStoreOp stencil_store_op) {
  auto *resource = getD3D12Resource(dsv.texture);
  dx12Log("Get DSV for resource {}", fmt::ptr(resource));
  // TODO: null descriptors
  assert(resource);
  auto [it, inserted] = m_dsvs[resource].insert(dsv.desc, {});
  auto &handle = std::get<1>(*it);
  if (inserted) {
    dx12Log("Create new DSV for resource {}", fmt::ptr(resource));
    handle = m_dsv_pool->allocate().cpu_handle;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
        .Format = getDXGIFormat(getDSVFormat(dsv)),
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray = {
            .MipSlice = dsv.desc.level,
            .FirstArraySlice = dsv.desc.layer,
            .ArraySize = 1,
        }};
    if (depth_store_op == TargetStoreOp::None) {
      dsv_desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    }
    if (stencil_store_op == TargetStoreOp::None) {
      dsv_desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    }
    m_device->CreateDepthStencilView(resource, &dsv_desc, handle);
  }
  return handle;
}

Descriptor DirectX12Device::getSRV(const SampledTextureView &srv) {
  auto *resource = getD3D12Resource(srv.texture);
  // TODO: null descriptors
  assert(resource);

  dx12Log("Get SRV for texture {}", fmt::ptr(resource));
#if 0
  dx12Debug("SRV desc: {}", srv.desc);
#endif

  auto [it, inserted] = m_texture_srvs[resource].insert(srv.desc, {});
  auto &descriptor = std::get<1>(*it);
  if (inserted) {
    dx12Log("Create new SRV for texture {}", fmt::ptr(resource));
    descriptor = m_cbv_srv_uav_pool->allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = getDXGIFormat(getSampledViewFormat(srv)),
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2DArray = {
            .MostDetailedMip = srv.desc.first_mip_level,
            .MipLevels = getSampledViewMipLevels(srv),
            .FirstArraySlice = srv.desc.first_array_layer,
            .ArraySize = getSampledViewArrayLayers(srv),
        }};
    m_device->CreateShaderResourceView(resource, &srv_desc,
                                       descriptor.cpu_handle);
  }

  return descriptor;
}

Descriptor DirectX12Device::getUAV(const StorageTextureView &uav) {
  auto *resource = getD3D12Resource(uav.texture);
  // TODO: null descriptors
  assert(resource);

  dx12Log("Get UAV for texture {}", fmt::ptr(resource));
#if 0
  dx12Debug("UAV desc: {}", uav.desc);
#endif

  auto [it, inserted] = m_texture_uavs[resource].insert(uav.desc, {});
  auto &descriptor = std::get<1>(*it);
  if (inserted) {
    dx12Log("Create new UAV for texture {}", fmt::ptr(resource));
    descriptor = m_cbv_srv_uav_pool->allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
        .Format = getDXGIFormat(getStorageViewFormat(uav)),
        .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray = {
            .MipSlice = uav.desc.mip_level,
            .FirstArraySlice = uav.desc.first_array_layer,
            .ArraySize = getStorageViewArrayLayers(uav),
        }};
    m_device->CreateUnorderedAccessView(resource, nullptr, &uav_desc,
                                        descriptor.cpu_handle);
  }

  return descriptor;
}

SyncObject DirectX12Device::createSyncObject(const SyncDesc &desc) {
  DIRECTX12_UNIMPLEMENTED;
}
} // namespace ren
