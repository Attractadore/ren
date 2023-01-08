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
    -> Optional<DescriptorSet> {
  DescriptorSet set;
  auto success = allocate_descriptor_sets(pool, {&layout, 1}, {&set, 1});
  if (success) {
    return std::move(set);
  }
  return None;
}

auto Device::allocate_descriptor_set(const DescriptorSetLayoutRef &layout)
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

void Device::write_descriptor_set(const DescriptorSetWriteConfig &config) {
  write_descriptor_sets({&config, 1});
}
