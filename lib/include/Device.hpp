#pragma once
#include "Buffer.hpp"
#include "DeleteQueue.hpp"
#include "RenderGraph.hpp"

struct RenDevice {
  virtual ~RenDevice() = default;

  virtual void begin_frame() = 0;
  virtual void end_frame() = 0;

  virtual std::unique_ptr<ren::RenderGraph::Builder>
  createRenderGraphBuilder() = 0;
  virtual ren::CommandAllocator &getCommandAllocator() = 0;

  virtual ren::Buffer create_buffer(const ren::BufferDesc &desc) = 0;

  virtual ren::Texture createTexture(const ren::TextureDesc &desc) = 0;

  virtual ren::SyncObject createSyncObject(const ren::SyncDesc &desc) = 0;

  virtual void
  push_to_delete_queue(ren::QueueCustomDeleter<ren::Device> deleter) = 0;

  template <std::convertible_to<ren::QueueCustomDeleter<ren::Device>> F>
  void push_to_delete_queue(F deleter) {
    push_to_delete_queue(std::move(deleter));
  }
};
