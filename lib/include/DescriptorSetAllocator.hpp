#pragma once
#include "Config.hpp"
#include "Descriptors.hpp"

namespace ren {

class Device;
class ResourceArena;

class DescriptorSetAllocator {
  unsigned m_frame_index = 0;
  std::array<Vector<Handle<DescriptorPool>>, c_pipeline_depth> m_frame_pools;
  std::array<unsigned, c_pipeline_depth> m_frame_pool_indices = {};

public:
  void next_frame(Device &device);

  auto allocate(Device &device, ResourceArena &arena,
                Handle<DescriptorSetLayout> layout) -> VkDescriptorSet;
};

} // namespace ren
