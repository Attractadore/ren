#pragma once

namespace ren {
class CommandBuffer;

class CommandAllocator {
public:
  virtual ~CommandAllocator() = default;

  virtual unsigned getPipelineDepth() const = 0;

  virtual CommandBuffer *allocateCommandBuffer() = 0;
  virtual void beginFrame() = 0;
  virtual void endFrame() = 0;
};
} // namespace ren
