#pragma once
#include "CommandBuffer.hpp"
#include "D3D12MA.hpp"
#include "Device.hpp"
#include "DirectX12CPUDescriptorPool.hpp"
#include "DirectX12CommandAllocator.hpp"
#include "DirectX12DeleteQueue.hpp"
#include "DirectX12PipelineCompiler.hpp"
#include "Errors.hpp"
#include "Support/Errors.hpp"
#include "Support/HashMap.hpp"
#include "Support/LinearMap.hpp"

#include <d3d12.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>

namespace ren {
class DirectX12Swapchain;

struct DirectX12DeviceTime {
  UINT64 direct_queue_time;
};

class DirectX12Device final : public Device {
  ComPtr<IDXGIFactory4> m_factory;
  ComPtr<IDXGIAdapter1> m_adapter;

#if REN_DIRECTX12_DEBUG
  struct LiveObjectReporter {
    LiveObjectReporter() = default;
    LiveObjectReporter(const LiveObjectReporter &) = default;
    LiveObjectReporter(LiveObjectReporter &&) = default;
    LiveObjectReporter &operator=(const LiveObjectReporter &) = default;
    LiveObjectReporter &operator=(LiveObjectReporter &&) = default;
    ~LiveObjectReporter() {
      ComPtr<IDXGIDebug> debug_controller;
      DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug_controller));
      debug_controller->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
    }
  } m_live_object_reporter;
#endif

  ComPtr<ID3D12Device> m_device;
  ComPtr<D3D12MA::Allocator> m_allocator;

  ComPtr<ID3D12CommandQueue> m_direct_queue;
  uint64_t m_direct_queue_time = 0;
  ComPtr<ID3D12Fence> m_fence;
  using Event = std::remove_pointer_t<HANDLE>;
  struct EventDeleter {
    void operator()(Event *e) const noexcept { CloseHandle(e); }
  };
  std::unique_ptr<Event, EventDeleter> m_event;

  DirectX12CPUDescriptorPool m_rtv_pool;
  DirectX12CPUDescriptorPool m_dsv_pool;
  DirectX12CPUDescriptorPool m_cbv_srv_uav_pool;
  HashMap<ID3D12Resource *,
          SmallLinearMap<RenderTargetViewDesc, D3D12_CPU_DESCRIPTOR_HANDLE, 3>>
      m_rtvs;
  HashMap<ID3D12Resource *,
          SmallLinearMap<DepthStencilViewDesc, D3D12_CPU_DESCRIPTOR_HANDLE, 3>>
      m_dsvs;
  HashMap<ID3D12Resource *, SmallLinearMap<SampledTextureViewDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE, 3>>
      m_texture_srvs;
  HashMap<ID3D12Resource *, SmallLinearMap<StorageTextureViewDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE, 3>>
      m_texture_uavs;

  DirectX12CommandAllocator m_cmd_alloc;

  DirectX12DeleteQueue m_delete_queue;

  unsigned m_frame_index = 0;
  std::array<DirectX12DeviceTime, c_pipeline_depth> m_frame_end_times = {};

private:
  void destroyTextureRTVs(ID3D12Resource *resource);
  void destroyTextureDSVs(ID3D12Resource *resource);
  void destroyTextureSRVs(ID3D12Resource *resource);
  void destroyTextureUAVs(ID3D12Resource *resource);

public:
  DirectX12Device(LUID adapter);
  DirectX12Device(const DirectX12Device &) = delete;
  DirectX12Device(DirectX12Device &&) = default;
  DirectX12Device &operator=(const DirectX12Device &) = delete;
  DirectX12Device &operator=(DirectX12Device &&) = default;
  ~DirectX12Device();

  auto *get() const { return m_device.Get(); }
  auto *getDXGIFactory() const { return m_factory.Get(); }
  DirectX12CommandAllocator &getDirectX12CommandAllocator() {
    return m_cmd_alloc;
  }
  CommandAllocator &getCommandAllocator() override {
    return getDirectX12CommandAllocator();
  }

  DirectX12PipelineCompiler &getDirectX12PipelineCompiler() {
    dx12Unimplemented();
  }
  PipelineCompiler &getPipelineCompiler() override {
    return getDirectX12PipelineCompiler();
  }

  void begin_frame() override;
  void end_frame() override;

  std::unique_ptr<DirectX12Swapchain> createSwapchain(HWND hwnd);

  std::unique_ptr<RenderGraph::Builder> createRenderGraphBuilder() override;

  Buffer create_buffer(const ren::BufferDesc &desc) override;
  auto get_buffer_device_address(const ren::BufferRef &buffer) const
      -> uint64_t override {
    dx12Unimplemented();
  }

  Texture createTexture(const ren::TextureDesc &desc) override;
  void destroyTextureViews(ID3D12Resource *resource);

  D3D12_CPU_DESCRIPTOR_HANDLE getRTV(const RenderTargetView &rtv);
  D3D12_CPU_DESCRIPTOR_HANDLE getDSV(const DepthStencilView &dsv,
                                     TargetStoreOp depth_store_op,
                                     TargetStoreOp stencil_store_op);
  D3D12_CPU_DESCRIPTOR_HANDLE getSRV(const SampledTextureView &srv);
  D3D12_CPU_DESCRIPTOR_HANDLE getUAV(const StorageTextureView &uav);

  SyncObject createSyncObject(const ren::SyncDesc &desc) override;

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

  ID3D12CommandQueue *getDirectQueue() const { return m_direct_queue.Get(); }

  void tickDirectQueue() {
    throwIfFailed(
        getDirectQueue()->Signal(m_fence.Get(), ++m_direct_queue_time),
        "D3D12: Failed to signal fence");
  }

  void directQueueSubmit(std::span<ID3D12CommandList *const> cmd_lists) {
    getDirectQueue()->ExecuteCommandLists(cmd_lists.size(), cmd_lists.data());
    tickDirectQueue();
  }

  void waitForDirectQueueCompletion(uint64_t time) const {
    if (m_fence->GetCompletedValue() < time) {
      throwIfFailed(m_fence->SetEventOnCompletion(time, m_event.get()),
                    "D3D12: Failed to set fence completion event");
      throwIfFailed(WaitForSingleObject(m_event.get(), INFINITE),
                    "WIN32: Failed to wait for event");
    }
  }

  void waitForDirectQueueCompletion() const {
    waitForDirectQueueCompletion(getDirectQueueTime());
  }

  uint64_t getDirectQueueTime() const { return m_direct_queue_time; }
  uint64_t getDirectQueueCompletedTime() const {
    return m_fence->GetCompletedValue();
  }

  template <std::derived_from<IUnknown> T> void push_to_delete_queue(T *value) {
    m_delete_queue.push<IUnknown *>(std::move(value));
  }

  template <typename T> void push_to_delete_queue(T value) {
    m_delete_queue.push(std::move(value));
  }

  void push_to_delete_queue(QueueCustomDeleter<Device> deleter) override {
    m_delete_queue.push(DirectX12QueueCustomDeleter(std::move(deleter)));
  }

  void flush();
};
} // namespace ren
