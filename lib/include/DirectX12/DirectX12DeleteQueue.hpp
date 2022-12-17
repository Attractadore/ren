#pragma once
#include "DeleteQueue.hpp"

#include <cstdint>

namespace ren {
class DirectX12Device;

template <> struct DeviceTime<DirectX12Device> {
  uint64_t direct_queue_time;

  auto operator<=>(const DeviceTime &) const = default;
};
using DirectX12DeviceTime = DeviceTime<DirectX12Device>;

using DirectX12QueueDeleter = QueueDeleter<DirectX12Device>;

using DirectX12DeleteQueue = DeleteQueue<DirectX12Device>;
} // namespace ren
