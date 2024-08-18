#pragma once
#include "Support/Vector.hpp"

#include <vulkan/vulkan.h>

namespace ren {

class Renderer;

class CommandAllocator {
  Renderer *m_renderer = nullptr;
  VkCommandPool m_pool = nullptr;
  Vector<VkCommandBuffer> m_cmd_buffers;
  unsigned m_allocated_count = 0;

private:
  void destroy();

public:
  explicit CommandAllocator(Renderer &renderer);
  CommandAllocator(const CommandAllocator &) = delete;
  CommandAllocator(CommandAllocator &&other) noexcept;
  CommandAllocator &operator=(const CommandAllocator &) = delete;
  CommandAllocator &operator=(CommandAllocator &&other) noexcept;
  ~CommandAllocator();

  auto allocate() -> VkCommandBuffer;

  void reset();
};

} // namespace ren
