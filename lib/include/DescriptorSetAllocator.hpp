#pragma once
#include "Config.hpp"
#include "Descriptors.hpp"

namespace ren {

class Device;

class DescriptorSetAllocator {
  Device *m_device;

  struct FrameDescriptorSetAllocator {
    Vector<DescriptorPool> pools;
    unsigned num_used;
  };

  unsigned m_frame_index = 0;
  std::array<FrameDescriptorSetAllocator, c_pipeline_depth> m_frame_pools;

  auto get_frame_allocator() -> FrameDescriptorSetAllocator & {
    return m_frame_pools[m_frame_index];
  }

public:
  DescriptorSetAllocator(Device &device);

  void next_frame();

  VkDescriptorSet allocate(const DescriptorSetLayoutRef &layout);
};

} // namespace ren
