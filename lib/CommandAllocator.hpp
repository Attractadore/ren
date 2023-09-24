#pragma once
#include "Config.hpp"
#include "Support/Vector.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class CommandPool {
  VkCommandPool m_pool = nullptr;
  Vector<VkCommandBuffer> m_cmd_buffers;
  unsigned m_allocated_count = 0;

private:
  void destroy();

public:
  CommandPool();
  CommandPool(const CommandPool &) = delete;
  CommandPool(CommandPool &&other);
  CommandPool &operator=(const CommandPool &) = delete;
  CommandPool &operator=(CommandPool &&other);
  ~CommandPool();

  VkCommandBuffer allocate();
  void reset();
};

class CommandAllocator {
public:
  void next_frame();

  auto allocate() -> VkCommandBuffer;

private:
  std::array<CommandPool, PIPELINE_DEPTH> m_frame_pools;
};

} // namespace ren
