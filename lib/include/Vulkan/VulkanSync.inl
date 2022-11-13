#pragma once
#include "VulkanSync.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace ren {
inline VkSemaphore getVkSemaphore(const SyncObject& sync) {
  assert(sync.desc.type == SyncType::Semaphore);
  return reinterpret_cast<VkSemaphore>(sync.handle.get());
}
}
