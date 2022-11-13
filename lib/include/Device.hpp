#pragma once
#include "RenderGraph.hpp"

struct RenDevice {
  virtual ~RenDevice() = default;

  virtual std::unique_ptr<ren::RenderGraphBuilder>
  createRenderGraphBuilder() = 0;
  virtual std::unique_ptr<ren::CommandAllocator>
  createCommandBufferPool(unsigned pipeline_depth) = 0;

  virtual ren::Texture createTexture(const ren::TextureDesc &desc) = 0;

  virtual ren::SyncObject createSyncObject(const ren::SyncDesc &desc) = 0;
};
