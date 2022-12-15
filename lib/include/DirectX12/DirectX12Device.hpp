#pragma once
#include "CommandBuffer.hpp"
#include "D3D12MA.hpp"
#include "Device.hpp"
#include "DirectX12CPUDescriptorPool.hpp"
#include "Support/Errors.hpp"
#include "Support/HashMap.hpp"
#include "Support/LinearMap.hpp"

#include <d3d12.h>
#include <dxgi1_2.h>

namespace ren {
class DirectX12Swapchain;

class DirectX12Device final : public Device {
  ComPtr<IDXGIFactory4> m_factory;
  ComPtr<IDXGIAdapter1> m_adapter;
  ComPtr<ID3D12Device> m_device;
  ComPtr<D3D12MA::Allocator> m_allocator;
  ComPtr<ID3D12CommandQueue> m_direct_queue;
  std::unique_ptr<DirectX12CPUDescriptorPool> m_rtv_pool;
  std::unique_ptr<DirectX12CPUDescriptorPool> m_dsv_pool;
  std::unique_ptr<DirectX12CPUDescriptorPool> m_cbv_srv_uav_pool;
  HashMap<ID3D12Resource *,
          SmallLinearMap<RenderTargetViewDesc, D3D12_CPU_DESCRIPTOR_HANDLE, 3>>
      m_rtvs;
  HashMap<ID3D12Resource *,
          SmallLinearMap<DepthStencilViewDesc, D3D12_CPU_DESCRIPTOR_HANDLE, 3>>
      m_dsvs;
  HashMap<ID3D12Resource *,
          SmallLinearMap<SampledTextureViewDesc, Descriptor, 3>>
      m_texture_srvs;
  HashMap<ID3D12Resource *,
          SmallLinearMap<StorageTextureViewDesc, Descriptor, 3>>
      m_texture_uavs;

private:
  void destroyResourceRTVs(ID3D12Resource *resource);
  void destroyResourceDSVs(ID3D12Resource *resource);
  void destroyResourceTextureSRVs(ID3D12Resource *resource);
  void destroyResourceTextureUAVs(ID3D12Resource *resource);
  void destroyTextureViews(ID3D12Resource *resource);

public:
  DirectX12Device(LUID adapter);

  auto *get() const { return m_device.Get(); }
  auto *getDXGIFactory() const { return m_factory.Get(); }

  std::unique_ptr<DirectX12Swapchain> createSwapchain(HWND hwnd);

  std::unique_ptr<RenderGraph::Builder> createRenderGraphBuilder() override;
  std::unique_ptr<ren::CommandAllocator>
  createCommandBufferPool(unsigned pipeline_depth) override;

  Texture createTexture(const ren::TextureDesc &desc) override;
  void destroyResourceData(ID3D12Resource *resource);

  D3D12_CPU_DESCRIPTOR_HANDLE getRTV(const RenderTargetView &rtv);
  D3D12_CPU_DESCRIPTOR_HANDLE getDSV(const DepthStencilView &dsv,
                                     TargetStoreOp depth_store_op,
                                     TargetStoreOp stencil_store_op);
  Descriptor getSRV(const SampledTextureView &srv);
  Descriptor getUAV(const StorageTextureView &uav);

  SyncObject createSyncObject(const ren::SyncDesc &desc) override;

  ID3D12CommandQueue *getDirectQueue() { return m_direct_queue.Get(); }

  ID3D12CommandAllocator *createCommandAllocator(D3D12_COMMAND_LIST_TYPE type) {
    ID3D12CommandAllocator *cmd_alloc;
    throwIfFailed(
        m_device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd_alloc)),
        "D3D12: Failed to create command allocator");
    return cmd_alloc;
  }

  ID3D12GraphicsCommandList *
  createCommandList(D3D12_COMMAND_LIST_TYPE type,
                    ID3D12CommandAllocator *pCommandAllocator,
                    ID3D12PipelineState *pInitialState) {
    ID3D12GraphicsCommandList *cmd_list;
    throwIfFailed(m_device->CreateCommandList(0, type, pCommandAllocator,
                                              pInitialState,
                                              IID_PPV_ARGS(&cmd_list)),
                  "D3D12: Failed to create command list");
    return cmd_list;
  }

  ID3D12Fence *createFence(UINT64 initial_value, D3D12_FENCE_FLAGS flags) {
    ID3D12Fence *fence;
    throwIfFailed(
        m_device->CreateFence(initial_value, flags, IID_PPV_ARGS(&fence)),
        "D3D12: Failed to create fence");
    return fence;
  }
};
} // namespace ren
