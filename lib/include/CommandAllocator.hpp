#pragma once
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Support/StableVector.hpp"
#include "Vulkan/VulkanCommandPool.hpp"

namespace ren {

class VulkanDevice;

class CommandAllocator {
  VulkanDevice *m_device;
  StaticVector<VulkanCommandPool, c_pipeline_depth> m_frame_pools;
  unsigned m_frame_index = 0;

public:
  explicit CommandAllocator(VulkanDevice &device);

  void begin_frame();
  void end_frame();

  CommandBuffer allocate();
};

} // namespace ren
