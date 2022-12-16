#pragma once
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

namespace ren {
class CommandBuffer;

class CommandAllocator {
  virtual void beginFrameImpl() = 0;
  virtual void endFrameImpl() = 0;

public:
  virtual ~CommandAllocator() = default;

  virtual unsigned getPipelineDepth() const;

  virtual CommandBuffer *allocateCommandBuffer() = 0;

  void beginFrame() { beginFrameImpl(); }
  void endFrame() { endFrameImpl(); }
};
} // namespace ren
