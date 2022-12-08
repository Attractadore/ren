#include "DirectX12/DirectX12CommandBuffer.hpp"
#include "DirectX12/DirectX12CommandAllocator.hpp"
#include "DirectX12/DirectX12Device.hpp"
#include "DirectX12/DirectX12Texture.hpp"
#include "DirectX12/Errors.hpp"
#include "Support/Views.hpp"

namespace ren {
DirectX12CommandBuffer::DirectX12CommandBuffer(
    DirectX12Device *device, DirectX12CommandAllocator *parent,
    ID3D12CommandAllocator *cmd_alloc) {
  m_device = device;
  m_parent = parent;
  m_cmd_list = m_device->createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           cmd_alloc, nullptr);
}

void DirectX12CommandBuffer::wait(SyncObject sync, PipelineStageFlags stages) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandBuffer::signal(SyncObject sync,
                                    PipelineStageFlags stages) {
  DIRECTX12_UNIMPLEMENTED;
}

namespace {
UINT getTargetSubresource(const TextureView &view, unsigned plane = 0) {
  const auto &tex_desc = view.texture.desc;
  const auto &sr = view.desc.subresource;
  return plane * (tex_desc.levels * tex_desc.layers) +
         tex_desc.levels * sr.first_layer + sr.first_mip_level;
}

void discardTarget(ID3D12GraphicsCommandList *cmd_list,
                   ID3D12Resource *resource, UINT subresource,
                   const D3D12_RECT &render_area) {
  D3D12_DISCARD_REGION discard_region = {
      .NumRects = 1,
      .pRects = &render_area,
      .FirstSubresource = subresource,
      .NumSubresources = 1,
  };
  cmd_list->DiscardResource(resource, &discard_region);
}
} // namespace

void DirectX12CommandBuffer::beginRendering(
    int x, int y, unsigned width, unsigned height,
    SmallVector<RenderTargetConfig, 8> render_targets,
    std::optional<DepthStencilTargetConfig> depth_stencil_target) {
  auto &rp = m_current_render_pass;
  rp.render_area = {
      .left = static_cast<LONG>(x),
      .top = static_cast<LONG>(y),
      .right = static_cast<LONG>(x + width),
      .bottom = static_cast<LONG>(y + height),
  };

  SmallVector<D3D12_CPU_DESCRIPTOR_HANDLE, 8> rtvs;
  for (auto &rt : render_targets) {
    auto rtv = m_device->getRTV(rt.view);
    rtvs.push_back(rtv);
    auto *resource = getD3D12Resource(rt.view.texture);
    auto sr = getTargetSubresource(rt.view);

    if (rt.load_op == TargetLoadOp::Clear) {
      m_cmd_list->ClearRenderTargetView(rtv, rt.clear_color.data(), 1,
                                        &rp.render_area);
    } else if (rt.load_op == TargetLoadOp::Discard) {
      discardTarget(m_cmd_list.Get(), resource, sr, rp.render_area);
    }

    if (rt.store_op == TargetStoreOp::Discard) {
      rp.discard_resources.push_back(resource);
      rp.discard_subresources.push_back(sr);
    }

    m_parent->addFrameResource(std::move(rt.view));
  }

  D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
  if (depth_stencil_target) {
    auto &dst = *depth_stencil_target;
    dsv = m_device->getDSV(dst.view);
    auto *resource = getD3D12Resource(dst.view.texture);
    auto depth_sr = getTargetSubresource(dst.view, 0);
    auto stencil_sr = getTargetSubresource(dst.view, 1);

    D3D12_CLEAR_FLAGS clear_flags{};
    if (dst.depth_load_op == TargetLoadOp::Clear) {
      clear_flags |= D3D12_CLEAR_FLAG_DEPTH;
    }
    if (dst.stencil_load_op == TargetLoadOp::Clear) {
      clear_flags |= D3D12_CLEAR_FLAG_STENCIL;
    }
    if (clear_flags) {
      m_cmd_list->ClearDepthStencilView(dsv, clear_flags, dst.clear_depth,
                                        dst.clear_stencil, 1, &rp.render_area);
    }

    if (dst.depth_load_op == TargetLoadOp::Discard) {
      discardTarget(m_cmd_list.Get(), resource, depth_sr, rp.render_area);
    }
    if (dst.stencil_load_op == TargetLoadOp::Discard) {
      discardTarget(m_cmd_list.Get(), resource, stencil_sr, rp.render_area);
    }

    if (dst.depth_store_op == TargetStoreOp::Discard) {
      rp.discard_resources.push_back(resource);
      rp.discard_subresources.push_back(depth_sr);
    }
    if (dst.stencil_store_op == TargetStoreOp::Discard) {
      rp.discard_resources.push_back(resource);
      rp.discard_subresources.push_back(stencil_sr);
    }

    m_parent->addFrameResource(std::move(dst.view));
  }

  m_cmd_list->OMSetRenderTargets(rtvs.size(), rtvs.data(), false,
                                 dsv.ptr ? &dsv : nullptr);
}

void DirectX12CommandBuffer::endRendering() {
  auto &rp = m_current_render_pass;
  for (auto &&[resource, sr] :
       ranges::views::zip(rp.discard_resources, rp.discard_subresources)) {
    discardTarget(m_cmd_list.Get(), resource, sr, rp.render_area);
  }
  rp.discard_resources.clear();
  rp.discard_subresources.clear();
}

void DirectX12CommandBuffer::blit(Texture src, Texture dst,
                                  std::span<const BlitRegion> regions,
                                  Filter filter) {
  DIRECTX12_UNIMPLEMENTED;
}

void DirectX12CommandBuffer::close() {
  throwIfFailed(m_cmd_list->Close(), "D3D12: Failed to record command list");
}

void DirectX12CommandBuffer::reset(ID3D12CommandAllocator *cmd_alloc) {
  throwIfFailed(m_cmd_list->Reset(cmd_alloc, nullptr),
                "D3D12: Failed to reset command list");
}
} // namespace ren
