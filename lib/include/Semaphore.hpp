#pragma once
#include "Support/Handle.hpp"

#include <vulkan/vulkan.h>

namespace ren {

struct Semaphore {
  SharedHandle<VkSemaphore> handle;
};

}; // namespace ren
