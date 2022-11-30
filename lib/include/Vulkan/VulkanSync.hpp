#pragma once
#include "Sync.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace ren {
enum class SyncType {
  Semaphore,
};

inline VkSemaphore getVkSemaphore(const SyncObject &sync) {
  assert(sync.desc.type == SyncType::Semaphore);
  return reinterpret_cast<VkSemaphore>(sync.handle.get());
}
}
