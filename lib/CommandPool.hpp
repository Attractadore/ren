#pragma once
#include "rhi.hpp"

namespace ren {

struct CommandPoolCreateInfo {
  rhi::QueueFamily queue_family = {};
};

struct CommandPool {
  rhi::CommandPool handle = {};
  rhi::QueueFamily queue_family = {};
};

} // namespace ren
