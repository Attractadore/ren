#pragma once
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Support/StableVector.hpp"

namespace ren {

class Device;

class CommandPool {
  Device *m_device = nullptr;
  VkCommandPool m_pool = VK_NULL_HANDLE;
  Vector<VkCommandBuffer> m_cmd_buffers;
  unsigned m_allocated_count = 0;

private:
  void destroy();

public:
  CommandPool(Device &device);
  CommandPool(const CommandPool &) = delete;
  CommandPool(CommandPool &&other);
  CommandPool &operator=(const CommandPool &) = delete;
  CommandPool &operator=(CommandPool &&other);
  ~CommandPool();

  VkCommandBuffer allocate();
  void reset();
};

class CommandAllocator {
  Device *m_device;
  StaticVector<CommandPool, c_pipeline_depth> m_frame_pools;
  unsigned m_frame_index = 0;

public:
  explicit CommandAllocator(Device &device);

  void next_frame();

  CommandBuffer allocate();
};

} // namespace ren
