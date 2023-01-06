#pragma once
#include "Config.hpp"
#include "Def.hpp"
#include "Descriptors.hpp"
#include "Support/FreeListAllocator.hpp"

namespace ren {

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

  void begin_frame();
  void end_frame();

  DescriptorSet allocate(const DescriptorSetLayoutRef &layout);
};

} // namespace ren
