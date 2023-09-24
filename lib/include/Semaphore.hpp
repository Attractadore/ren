#pragma once
#include "DebugNames.hpp"
#include "Support/Optional.hpp"
#include "Support/StdDef.hpp"

#include <vulkan/vulkan.h>

namespace ren {

struct SemaphoreCreateInfo {
  REN_DEBUG_NAME_FIELD("Semaphore");
  Optional<u64> initial_value;
};

struct Semaphore {
  VkSemaphore handle;
};

}; // namespace ren
