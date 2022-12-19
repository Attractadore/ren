#pragma once
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

namespace ren {
class CommandBuffer;

struct CommandAllocator {
  virtual ~CommandAllocator() = default;

  virtual CommandBuffer *allocateCommandBuffer() = 0;
};
} // namespace ren
