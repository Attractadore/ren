#pragma once

namespace ren {
class CommandBuffer;

struct CommandAllocator {
  virtual ~CommandAllocator() = default;

  virtual void begin_frame() = 0;
  virtual void end_frame() = 0;

  virtual CommandBuffer *allocateCommandBuffer() = 0;
};
} // namespace ren
