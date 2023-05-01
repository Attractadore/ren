#include "DescriptorSetAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"

namespace ren {

void DescriptorSetAllocator::next_frame(Device &device) {
  m_frame_index = (m_frame_index + 1) % m_frame_pools.size();
  for (auto &pool : m_frame_pools[m_frame_index]) {
    device.reset_descriptor_pool(pool);
  }
  m_frame_pool_indices[m_frame_index] = 0;
}

auto DescriptorSetAllocator::allocate(Device &device,
                                      DescriptorSetLayoutRef layout)
    -> VkDescriptorSet {
  auto &pool_index = m_frame_pool_indices[m_frame_index];
  auto &pools = m_frame_pools[m_frame_index];

  while (pool_index < pools.size()) {
    auto set = device.allocate_descriptor_set(pools[pool_index], layout);
    if (set) {
      return *set;
    }
    pool_index++;
  }

  DescriptorPoolDesc pool_desc = {.set_count = 16};
  pool_desc.pool_sizes.fill(16);
  auto &pool = pools.emplace_back(device.create_descriptor_pool(pool_desc));

  auto set = device.allocate_descriptor_set(pool, layout);
  if (!set) {
    throw std::runtime_error{"Failed to allocate descriptor set"};
  }

  return std::move(*set);
}

} // namespace ren
