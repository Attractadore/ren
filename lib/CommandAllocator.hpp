#pragma once
#include "Config.hpp"
#include "Support/Vector.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class Renderer;

class CommandPool {
  Renderer *m_renderer = nullptr;
  VkCommandPool m_pool = nullptr;
  Vector<VkCommandBuffer> m_cmd_buffers;
  unsigned m_allocated_count = 0;

private:
  void destroy();

public:
  explicit CommandPool(Renderer &renderer);
  CommandPool(const CommandPool &) = delete;
  CommandPool(CommandPool &&other) noexcept;
  CommandPool &operator=(const CommandPool &) = delete;
  CommandPool &operator=(CommandPool &&other) noexcept;
  ~CommandPool();

  VkCommandBuffer allocate();
  void reset();
};

class CommandAllocator {
public:
  explicit CommandAllocator(Renderer &renderer);

  void next_frame();

  auto allocate() -> VkCommandBuffer;

private:
  StaticVector<CommandPool, PIPELINE_DEPTH> m_frame_pools;
};

} // namespace ren
