#pragma once
#include "DeleteQueue.hpp"
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "Reflection.hpp"
#include "RenderGraph.hpp"

namespace ren {
class MaterialPipelineCompiler;

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

  [[nodiscard]] virtual auto
  create_descriptor_pool(const DescriptorPoolDesc &desc) -> DescriptorPool = 0;

  virtual void reset_descriptor_pool(const DescriptorPoolRef &pool) = 0;

  [[nodiscard]] virtual auto
  create_descriptor_set_layout(const DescriptorSetLayoutDesc &desc)
      -> DescriptorSetLayout = 0;

  [[nodiscard]] virtual auto
  allocate_descriptor_sets(const DescriptorPoolRef &pool,
                           std::span<const DescriptorSetLayoutRef> layouts,
                           std::span<DescriptorSet> sets) -> bool = 0;

  [[nodiscard]] auto
  allocate_descriptor_set(const DescriptorPoolRef &pool,
                          const DescriptorSetLayoutRef &layout)
      -> Optional<DescriptorSet> {
    DescriptorSet set;
    auto success = allocate_descriptor_sets(pool, {&layout, 1}, {&set, 1});
    if (success) {
      return std::move(set);
    }
    return None;
  }

  [[nodiscard]] auto
  allocate_descriptor_set(const DescriptorSetLayoutRef &layout)
      -> std::pair<DescriptorPool, DescriptorSet> {
    DescriptorPoolDesc pool_desc = {.set_count = 1};
    if (layout.desc->flags.isSet(DescriptorSetLayoutOption::UpdateAfterBind)) {
      pool_desc.flags |= DescriptorPoolOption::UpdateAfterBind;
    }
    for (const auto &binding : layout.desc->bindings) {
      pool_desc.descriptor_counts[binding.type] += binding.count;
    }
    auto pool = create_descriptor_pool(pool_desc);
    auto set = allocate_descriptor_set(pool, layout);
    assert(set);
    return {std::move(pool), std::move(set.value())};
  }

  virtual void
  write_descriptor_sets(std::span<const DescriptorSetWriteConfig> configs) = 0;
  void write_descriptor_set(const DescriptorSetWriteConfig &config) {
    write_descriptor_sets({&config, 1});
  }

  virtual Buffer create_buffer(const BufferDesc &desc) = 0;
  virtual auto get_buffer_device_address(const BufferRef &buffer) const
      -> uint64_t = 0;

  virtual Texture createTexture(const TextureDesc &desc) = 0;

  virtual SyncObject createSyncObject(const SyncDesc &desc) = 0;

  [[nodiscard]] virtual auto get_shader_blob_suffix() const
      -> std::string_view = 0;
  [[nodiscard]] virtual auto get_shader_reflection_suffix() const
      -> std::string_view = 0;
  [[nodiscard]] virtual auto
  create_graphics_pipeline(const GraphicsPipelineConfig &desc) -> Pipeline = 0;
  [[nodiscard]] virtual auto
  create_reflection_module(std::span<const std::byte> data)
      -> std::unique_ptr<ReflectionModule> = 0;
  [[nodiscard]] virtual auto
  create_pipeline_signature(const PipelineSignatureDesc &desc)
      -> PipelineSignature = 0;

  virtual void push_to_delete_queue(QueueCustomDeleter<Device> deleter) = 0;

  template <std::convertible_to<QueueCustomDeleter<Device>> F>
  void push_to_delete_queue(F deleter) {
    push_to_delete_queue(QueueCustomDeleter<Device>(std::move(deleter)));
  }
};
