#pragma once
#include "Config.hpp"
#include "Descriptors.hpp"

namespace ren {

class Device;

class DescriptorSetAllocator {
  unsigned m_frame_index = 0;
  std::array<Vector<DescriptorPool>, c_pipeline_depth> m_frame_pools;
  std::array<unsigned, c_pipeline_depth> m_frame_pool_indices = {};

public:
  void next_frame(Device &device);

  VkDescriptorSet allocate(Device &device, DescriptorSetLayoutRef layout);
};

} // namespace ren
