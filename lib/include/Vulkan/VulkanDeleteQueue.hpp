#pragma once
#include "DeleteQueue.hpp"

#include <cstdint>

namespace ren {
class VulkanDevice;

template <> struct DeviceTime<VulkanDevice> {
  uint64_t graphics_queue_time;

  auto operator<=>(const DeviceTime &other) const = default;
};
using VulkanDeviceTime = DeviceTime<VulkanDevice>;

using VulkanQueueDeleter = QueueDeleter<VulkanDevice>;

using VulkanDeleteQueue = DeleteQueue<VulkanDevice>;
} // namespace ren
