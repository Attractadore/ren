#pragma once
#include "CommandBuffer.hpp"
#include "DirectX12Descriptor.hpp"
#include "Errors.hpp"
#include "Support/ComPtr.hpp"

#include <d3d12.h>

namespace ren {
class DirectX12CommandAllocator;
class DirectX12Device;

class DirectX12CommandBuffer final : public CommandBuffer {
  struct RenderPassInfo {
    D3D12_RECT render_area;
    SmallVector<ID3D12Resource *, 8> discard_resources;
    SmallVector<UINT, 8> discard_subresources;
  };

  DirectX12Device *m_device;
  DirectX12CommandAllocator *m_parent;
  ComPtr<ID3D12GraphicsCommandList> m_cmd_list;
  RenderPassInfo m_current_render_pass;

public:
  DirectX12CommandBuffer(DirectX12Device *device,
                         DirectX12CommandAllocator *parent,
                         ID3D12CommandAllocator *cmd_alloc);

  void beginRendering(
      int x, int y, unsigned width, unsigned height,
      SmallVector<RenderTargetConfig, 8> render_targets,
      std::optional<DepthStencilTargetConfig> depth_stencil_target) override;
  using CommandBuffer::beginRendering;
  void endRendering() override;

  void copy_buffer(const BufferRef &src, const BufferRef &dst,
                   std::span<const CopyRegion> regions) override;

  void set_viewports(std::span<const Viewport> viewports) override {
    dx12Unimplemented();
  }

  void set_scissor_rects(std::span<const ScissorRect> rects) override {
    dx12Unimplemented();
  }

  void bind_graphics_pipeline(const PipelineRef &pipeline) override {
    dx12Unimplemented();
  }

  void set_graphics_push_constants(const PipelineSignature &signature,
                                   ShaderStageFlags stages,
                                   std::span<const std::byte> data,
                                   unsigned offset) override {
    dx12Unimplemented();
  }

  void bind_index_buffer(const BufferRef &buffer, IndexFormat format) override {
    dx12Unimplemented();
  }

  void draw_indexed(unsigned num_indices, unsigned num_instances,
                    unsigned first_index, int vertex_offset,
                    unsigned first_instance) override {
    dx12Unimplemented();
  }

  void close() override;
  void reset(ID3D12CommandAllocator *command_allocator);

  ID3D12GraphicsCommandList *get() const { return m_cmd_list.Get(); }
  DirectX12CommandAllocator *getParent() const { return m_parent; }
  DirectX12Device *getDevice() const { return m_device; }

  Descriptor allocateDescriptors(unsigned count);
};
} // namespace ren
