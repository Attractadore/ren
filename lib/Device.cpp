#include "Device.hpp"

auto Device::create_buffer(BufferDesc desc) -> Buffer {
  assert(desc.offset == 0);
  assert(!desc.ptr);
  if (desc.size == 0) {
    return {.desc = desc};
  }
  auto [handle, ptr] = create_buffer_handle(desc);
  desc.ptr = ptr;
  return {.desc = desc, .handle = std::move(handle)};
}

auto Device::allocate_descriptor_set(const DescriptorPoolRef &pool,
                                     const DescriptorSetLayoutRef &layout)
    -> Optional<VkDescriptorSet> {
  VkDescriptorSet set;
  auto success = allocate_descriptor_sets(pool, {&layout, 1}, {&set, 1});
  if (success) {
    return std::move(set);
  }
  return None;
}

auto Device::allocate_descriptor_set(const DescriptorSetLayoutRef &layout)
    -> std::pair<DescriptorPool, VkDescriptorSet> {
  DescriptorPoolDesc pool_desc = {.set_count = 1};
  if (layout.desc->flags.isSet(DescriptorSetLayoutOption::UpdateAfterBind)) {
    pool_desc.flags |= DescriptorPoolOption::UpdateAfterBind;
  }
  for (const auto &binding : layout.desc->bindings) {
    pool_desc.pool_sizes[binding.type] += binding.count;
  }
  auto pool = create_descriptor_pool(pool_desc);
  auto set = allocate_descriptor_set(pool, layout);
  assert(set);
  return {std::move(pool), std::move(set.value())};
}

void Device::write_descriptor_set(const VkWriteDescriptorSet &config) {
  write_descriptor_sets({&config, 1});
}

auto Device::create_graphics_pipeline(GraphicsPipelineConfig config)
    -> GraphicsPipeline {
  auto handle = create_graphics_pipeline_handle(config);
  return {
      .desc = std::make_shared<GraphicsPipelineDesc>(std::move(config.desc)),
      .handle = std::move(handle),
  };
}

auto Device::supports_buffer_device_address() const -> bool {
  return supports_feature(DeviceFeature::BufferDeviceAddress);
}
