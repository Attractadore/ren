#include "DescriptorSetAllocator.hpp"
#include "Device.hpp"
#include "Errors.hpp"
#include "ResourceArena.hpp"

namespace ren {

void DescriptorSetAllocator::next_frame(Device &device) {
  m_frame_index = (m_frame_index + 1) % m_frame_pools.size();
  for (auto pool : m_frame_pools[m_frame_index]) {
    device.reset_descriptor_pool(pool);
  }
  m_frame_pool_indices[m_frame_index] = 0;
}

auto DescriptorSetAllocator::allocate(Device &device, ResourceArena &arena,
                                      Handle<DescriptorSetLayout> layout)
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

  std::array<unsigned, DESCRIPTOR_TYPE_COUNT> pool_sizes;
  pool_sizes.fill(16);

  auto pool = arena.create_descriptor_pool({
      .set_count = 16,
      .pool_sizes = pool_sizes,
  });
  pools.push_back(pool);

  auto set = device.allocate_descriptor_set(pool, layout);
  if (!set) {
    throw std::runtime_error{"Failed to allocate descriptor set"};
  }

  return *set;
}

} // namespace ren
