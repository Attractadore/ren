#include "DescriptorSetAllocator.hpp"
#include "Device.hpp"
#include "Support/Errors.hpp"

namespace ren {

DescriptorSetAllocator::DescriptorSetAllocator(Device &device)
    : m_device(&device) {}

void DescriptorSetAllocator::begin_frame() {
  m_frame_index = (m_frame_index + 1) % m_frame_pools.size();
  auto &alloc = get_frame_allocator();
  for (auto &pool : alloc.pools) {
    m_device->reset_descriptor_pool(pool);
  }
  alloc.num_used = 0;
}

void DescriptorSetAllocator::end_frame() {}

auto DescriptorSetAllocator::allocate(const DescriptorSetLayoutRef &layout)
    -> DescriptorSet {
  auto &alloc = get_frame_allocator();

  while (alloc.num_used < alloc.pools.size()) {
    auto &pool = alloc.pools[alloc.num_used];
    auto set = m_device->allocate_descriptor_set(pool, layout);
    if (set) {
      return std::move(*set);
    }
    alloc.num_used++;
  }

  DescriptorPoolDesc pool_desc = {.set_count = 16};
  pool_desc.pool_sizes.fill(16);
  auto &pool =
      alloc.pools.emplace_back(m_device->create_descriptor_pool(pool_desc));

  auto set = m_device->allocate_descriptor_set(pool, layout);
  if (!set) {
    throw std::runtime_error{"Failed to allocate descriptor set"};
  }

  return std::move(*set);
}

} // namespace ren
