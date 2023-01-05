#pragma once
#include "Buffer.hpp"
#include "DeleteQueue.hpp"
#include "RenderGraph.hpp"

namespace ren {
class PipelineCompiler;

enum class QueueType {
  Graphics,
  Compute,
  Transfer,
};
} // namespace ren

using namespace ren;

struct RenDevice {
  virtual ~RenDevice() = default;

  virtual void begin_frame() = 0;
  virtual void end_frame() = 0;

  virtual std::unique_ptr<RenderGraph::Builder> createRenderGraphBuilder() = 0;

  virtual auto
  create_command_allocator(QueueType queue_type = QueueType::Graphics)
      -> std::unique_ptr<CommandAllocator> = 0;

  virtual auto create_pipeline_compiler()
      -> std::unique_ptr<PipelineCompiler> = 0;

  virtual Buffer create_buffer(const BufferDesc &desc) = 0;
  virtual auto get_buffer_device_address(const BufferRef &buffer) const
      -> uint64_t = 0;

  virtual Texture createTexture(const TextureDesc &desc) = 0;

  virtual SyncObject createSyncObject(const SyncDesc &desc) = 0;

  virtual void push_to_delete_queue(QueueCustomDeleter<Device> deleter) = 0;

  template <std::convertible_to<QueueCustomDeleter<Device>> F>
  void push_to_delete_queue(F deleter) {
    push_to_delete_queue(QueueCustomDeleter<Device>(std::move(deleter)));
  }
};
