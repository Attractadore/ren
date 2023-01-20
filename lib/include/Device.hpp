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

enum class DeviceFeature {
  BufferDeviceAddress,
};

} // namespace ren

using namespace ren;

struct RenDevice {
protected:
  [[nodiscard]] virtual auto create_buffer_handle(const BufferDesc &desc)
      -> std::pair<SharedHandle<VkBuffer>, void *> = 0;

  [[nodiscard]] virtual auto
  create_graphics_pipeline_handle(const GraphicsPipelineConfig &config)
      -> SharedHandle<VkPipeline> = 0;

public:
  virtual ~RenDevice() = default;

  virtual void begin_frame() = 0;
  virtual void end_frame() = 0;

  virtual bool supports_feature(DeviceFeature feature) const = 0;

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
      -> Optional<DescriptorSet>;

  [[nodiscard]] auto
  allocate_descriptor_set(const DescriptorSetLayoutRef &layout)
      -> std::pair<DescriptorPool, DescriptorSet>;

  virtual void
  write_descriptor_sets(std::span<const DescriptorSetWriteConfig> configs) = 0;
  void write_descriptor_set(const DescriptorSetWriteConfig &config);

  auto create_buffer(BufferDesc desc) -> Buffer;

  auto supports_buffer_device_address() const -> bool;

  virtual auto get_buffer_device_address(const BufferRef &buffer) const
      -> uint64_t = 0;

  virtual Texture createTexture(const TextureDesc &desc) = 0;

  [[nodiscard]] virtual auto get_shader_blob_suffix() const
      -> std::string_view = 0;
  [[nodiscard]] virtual auto get_shader_reflection_suffix() const
      -> std::string_view = 0;

  [[nodiscard]] auto create_graphics_pipeline(GraphicsPipelineConfig config)
      -> GraphicsPipeline;

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
