#pragma once
#include "Buffer.hpp"
#include "RenderGraph.hpp"

struct RenDevice {
  virtual ~RenDevice() = default;

  virtual void begin_frame() = 0;
  virtual void end_frame() = 0;

  virtual std::unique_ptr<ren::RenderGraph::Builder>
  createRenderGraphBuilder() = 0;
  virtual ren::CommandAllocator &getCommandAllocator() = 0;

  virtual bool is_uma() const = 0;

  virtual ren::Buffer create_buffer(const ren::BufferDesc &desc) = 0;

  virtual ren::Texture createTexture(const ren::TextureDesc &desc) = 0;

  virtual ren::SyncObject createSyncObject(const ren::SyncDesc &desc) = 0;
};
