#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DXGIFormat.hpp"
#include "DirectX12/DirectX12Buffer.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12DeleteQueue.inl"
#include "DirectX12/DirectX12RenderGraph.hpp"
#include "DirectX12/DirectX12Swapchain.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"

#include <d3d12sdklayers.h>

#include <iostream>

namespace ren {
namespace {
LONG NTAPI debugHandler(PEXCEPTION_POINTERS exception) {
  auto record = exception->ExceptionRecord;
  if (record->NumberParameters >= 2) {
    switch (record->ExceptionCode) {
    case DBG_PRINTEXCEPTION_C: {
      auto *str = reinterpret_cast<PCSTR>(record->ExceptionInformation[1]);
      std::cerr << str;
      break;
    }
    case DBG_PRINTEXCEPTION_WIDE_C: {
      auto *str = reinterpret_cast<PCWSTR>(record->ExceptionInformation[1]);
      std::wcerr << str;
      break;
    }
    }
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

DirectX12Device::DirectX12Device(LUID luid)
    : m_factory([&] {
        constexpr UINT factory_flags =
#if REN_DIRECTX12_DEBUG
            DXGI_CREATE_FACTORY_DEBUG |
#endif
            0;

        IDXGIFactory4 *factory;
        throwIfFailed(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)),
                      "DXGI: Failed to create factory");

        return factory;
      }()),
      m_adapter([&] {
        IDXGIAdapter1 *adapter;
#if REN_DIRECTX12_FORCE_WARP_DEVICE
        throwIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),
                      "DXGI: Failed to find WARP adapter");
#else
        throwIfFailed(
            m_factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)),
            "DXGI: Failed to find adapter");
#endif
        return adapter;
      }()),
      m_device([&] {
#if REN_DIRECTX12_DEBUG
        {
          ComPtr<ID3D12Debug5> debug_controller;
          throwIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)),
                        "D3D12: Failed to get debug controller");
          debug_controller->EnableDebugLayer();
          debug_controller->SetEnableAutoName(true);
        }
#endif

        ID3D12Device *device;
        throwIfFailed(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&device)),
                      "D3D12: Failed to create device");

        return device;
      }()),
      m_rtv_pool(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
      m_dsv_pool(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
      m_cbv_srv_uav_pool(m_device.Get(),
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
#if REN_DIRECTX12_DEBUG
  {
    ComPtr<ID3D12InfoQueue1> info_queue;
    if (SUCCEEDED(m_device->QueryInterface(info_queue.GetAddressOf()))) {
      auto debug_callback = [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY,
                               D3D12_MESSAGE_ID, LPCSTR pDescription,
                               void *) { std::wcerr << pDescription << "\n"; };
      DWORD cookie;
      throwIfFailed(info_queue->RegisterMessageCallback(
                        debug_callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                        nullptr, &cookie),
                    "D3D12: Failed to set debug callback");
    } else {
      AddVectoredExceptionHandler(true, debugHandler);
    }
  }
#endif

  D3D12MA::ALLOCATOR_DESC allocator_desc = {
      .pDevice = m_device.Get(),
      .pAdapter = m_adapter.Get(),
  };

  throwIfFailed(
      D3D12MA::CreateAllocator(&allocator_desc, m_allocator.GetAddressOf()),
      "D3D12MA: Failed to create allocator");

  D3D12_COMMAND_QUEUE_DESC queue_desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
  };

  throwIfFailed(
      m_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_direct_queue)),
      "D3D12: Failed to create graphics queue");

  throwIfFailed(
      m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
      "D3D12: Failed to create fence");
  m_event.reset(CreateEvent(nullptr, false, false, nullptr));
  throwIfFailed(m_event.get(), "WIN32: Failed to create event handle");
}

DirectX12Device::~DirectX12Device() { flush(); }

auto DirectX12Device::supports_feature(DeviceFeature feature) const -> bool {
  switch (feature) {
    using enum DeviceFeature;
  default:
    return false;
  }
}

void DirectX12Device::flush() {
  waitForDirectQueueCompletion();
  m_delete_queue.flush(*this);
}

void DirectX12Device::begin_frame() {
  m_frame_index = (m_frame_index + 1) % c_pipeline_depth;
  waitForDirectQueueCompletion(
      m_frame_end_times[m_frame_index].direct_queue_time);
  m_delete_queue.begin_frame(*this);
}

void DirectX12Device::end_frame() {
  m_delete_queue.end_frame(*this);
  m_frame_end_times[m_frame_index].direct_queue_time = getDirectQueueTime();
}

std::unique_ptr<DirectX12Swapchain>
DirectX12Device::createSwapchain(HWND hwnd) {
  return std::make_unique<DirectX12Swapchain>(this, hwnd);
}

std::unique_ptr<RenderGraph::Builder>
DirectX12Device::createRenderGraphBuilder() {
  return std::make_unique<DirectX12RenderGraph::Builder>(this);
}

auto DirectX12Device::create_command_allocator(QueueType queue_type)
    -> std::unique_ptr<CommandAllocator> {
  return std::make_unique<DirectX12CommandAllocator>(*this);
}

auto DirectX12Device::create_buffer_handle(const BufferDesc &desc)
    -> std::pair<AnyRef, void *> {
  auto get_resource_state = [](D3D12_HEAP_TYPE heap_type) {
    switch (heap_type) {
    case D3D12_HEAP_TYPE_DEFAULT:
      return D3D12_RESOURCE_STATE_COMMON;
    case D3D12_HEAP_TYPE_UPLOAD:
      return D3D12_RESOURCE_STATE_GENERIC_READ;
    case D3D12_HEAP_TYPE_READBACK:
      return D3D12_RESOURCE_STATE_COPY_DEST;
    }
  };

  D3D12MA::ALLOCATION_DESC alloc_desc = {.HeapType =
                                             getD3D12HeapType(desc.heap)};

  D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = desc.size,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {.Count = 1},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      .Flags = getD3D12ResourceFlags(desc.usage),
  };

  D3D12MA::Allocation *allocation;
  throwIfFailed(
      m_allocator->CreateResource(&alloc_desc, &resource_desc,
                                  get_resource_state(alloc_desc.HeapType),
                                  nullptr, &allocation, IID_NULL, nullptr),
      "D3D12MA: Failed to create buffer");
  auto *resource = allocation->GetResource();

  void *map = nullptr;
  if (alloc_desc.HeapType == D3D12_HEAP_TYPE_UPLOAD) {
    D3D12_RANGE range = {0, 0};
    throwIfFailed(resource->Map(0, &range, &map),
                  "D3D12: Failed to map buffer");
  } else if (alloc_desc.HeapType == D3D12_HEAP_TYPE_READBACK) {
    throwIfFailed(resource->Map(0, nullptr, &map),
                  "D3D12: Failed to map buffer");
  }

  return {AnyRef(resource,
                 [this, allocation](ID3D12Resource *) {
                   push_to_delete_queue(allocation);
                 }),
          map};
}

Texture DirectX12Device::createTexture(const ren::TextureDesc &desc) {
  auto dxgi_format = getDXGIFormat(desc.format);
  D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = getD3D12ResourceDimension(desc.type),
      .Width = desc.width,
      .Height = desc.height,
      .DepthOrArraySize = desc.array_layers,
      .MipLevels = desc.mip_levels,
      .Format = dxgi_format,
      .SampleDesc = {.Count = 1},
      .Flags = getD3D12ResourceFlags(desc.usage),
  };

  D3D12MA::ALLOCATION_DESC allocation_desc = {.HeapType =
                                                  D3D12_HEAP_TYPE_DEFAULT};

  D3D12_CLEAR_VALUE clear_color = {.Format = dxgi_format,
                                   .Color = {0.0f, 0.0f, 0.0f, 1.0f}};
  D3D12_CLEAR_VALUE clear_depth = {.Format = dxgi_format,
                                   .DepthStencil = {.Depth = 1.0f}};

  D3D12MA::Allocation *allocation;
  throwIfFailed(m_allocator->CreateResource(
                    &allocation_desc, &resource_desc,
                    D3D12_RESOURCE_STATE_COMMON,
                    isColorFormat(desc.format) ? &clear_color : &clear_depth,
                    &allocation, IID_NULL, nullptr),
                "D3D12MA: Failed to create texture");

  return {.desc = desc,
          .handle =
              AnyRef(allocation->GetResource(),
                     [this, allocation](ID3D12Resource *resource) {
                       push_to_delete_queue(DirectX12TextureViews{resource});
                       push_to_delete_queue(allocation);
                     })};
}

void DirectX12Device::destroyTextureViews(ID3D12Resource *resource) {
  destroyTextureRTVs(resource);
  destroyTextureDSVs(resource);
  destroyTextureSRVs(resource);
  destroyTextureUAVs(resource);
}

void DirectX12Device::destroyTextureRTVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_rtvs[resource]) {
    m_rtv_pool.free(desciptor);
  }
  m_rtvs.erase(resource);
}

void DirectX12Device::destroyTextureDSVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_dsvs[resource]) {
    m_dsv_pool.free(desciptor);
  }
  m_dsvs.erase(resource);
}

void DirectX12Device::destroyTextureSRVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_texture_srvs[resource]) {
    m_cbv_srv_uav_pool.free(desciptor);
  }
  m_texture_srvs.erase(resource);
}

void DirectX12Device::destroyTextureUAVs(ID3D12Resource *resource) {
  for (auto &&[_, desciptor] : m_texture_uavs[resource]) {
    m_cbv_srv_uav_pool.free(desciptor);
  }
  m_texture_uavs.erase(resource);
}

D3D12_CPU_DESCRIPTOR_HANDLE
DirectX12Device::getRTV(const RenderTargetView &rtv) {
  auto *resource = getD3D12Resource(rtv.texture);
  // TODO: null descriptors
  assert(resource);

  auto [it, inserted] = m_rtvs[resource].insert(rtv.desc, {});
  auto &handle = std::get<1>(*it);
  if (inserted) {
    handle = m_rtv_pool.allocate();
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
        .Format = getDXGIFormat(rtv.desc.format),
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray = {
            .MipSlice = rtv.desc.mip_level,
            .FirstArraySlice = rtv.desc.array_layer,
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
  // TODO: null descriptors
  assert(resource);

  auto [it, inserted] = m_dsvs[resource].insert(dsv.desc, {});
  auto &handle = std::get<1>(*it);
  if (inserted) {
    handle = m_dsv_pool.allocate();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
        .Format = getDXGIFormat(dsv.texture.desc.format),
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray = {
            .MipSlice = dsv.desc.mip_level,
            .FirstArraySlice = dsv.desc.array_layer,
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

D3D12_CPU_DESCRIPTOR_HANDLE
DirectX12Device::getSRV(const SampledTextureView &srv) {
  auto *resource = getD3D12Resource(srv.texture);
  // TODO: null descriptors
  assert(resource);

  auto [it, inserted] = m_texture_srvs[resource].insert(srv.desc, {});
  auto &descriptor = std::get<1>(*it);
  if (inserted) {
    descriptor = m_cbv_srv_uav_pool.allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = getDXGIFormat(srv.desc.format),
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2DArray = {
            .MostDetailedMip = srv.desc.first_mip_level,
            .MipLevels = srv.desc.mip_levels,
            .FirstArraySlice = srv.desc.first_array_layer,
            .ArraySize = srv.desc.array_layers,
        }};
    m_device->CreateShaderResourceView(resource, &srv_desc, descriptor);
  }

  return descriptor;
}

D3D12_CPU_DESCRIPTOR_HANDLE
DirectX12Device::getUAV(const StorageTextureView &uav) {
  auto *resource = getD3D12Resource(uav.texture);
  // TODO: null descriptors
  assert(resource);

  auto [it, inserted] = m_texture_uavs[resource].insert(uav.desc, {});
  auto &descriptor = std::get<1>(*it);
  if (inserted) {
    descriptor = m_cbv_srv_uav_pool.allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
        .Format = getDXGIFormat(uav.desc.format),
        .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray = {
            .MipSlice = uav.desc.mip_level,
            .FirstArraySlice = uav.desc.first_array_layer,
            .ArraySize = uav.desc.array_layers,
        }};
    m_device->CreateUnorderedAccessView(resource, nullptr, &uav_desc,
                                        descriptor);
  }

  return descriptor;
}

SyncObject DirectX12Device::createSyncObject(const SyncDesc &desc) {
  DIRECTX12_UNIMPLEMENTED;
}
} // namespace ren
