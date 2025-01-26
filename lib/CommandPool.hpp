#pragma once
#include "DebugNames.hpp"
#include "rhi.hpp"

namespace ren {

struct CommandPoolCreateInfo {
  REN_DEBUG_NAME_FIELD("Command pool");
  rhi::QueueFamily queue_family = {};
};

struct CommandPool {
  rhi::CommandPool handle = {};
  rhi::QueueFamily queue_family = {};
};

} // namespace ren
