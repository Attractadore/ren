#pragma once
#include "Support/Handle.hpp"

#include <vulkan/vulkan.h>

namespace ren {

struct SemaphoreRef {
  VkSemaphore handle;
};

struct Semaphore {
  SharedHandle<VkSemaphore> handle;

  operator SemaphoreRef() const noexcept { return {.handle = handle.get()}; }
};

}; // namespace ren
