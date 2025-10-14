#pragma once
#include "ren/core/String.hpp"
#include "rhi.hpp"

namespace ren {

struct CommandPoolCreateInfo {
  String8 name = "Command pool";
  rhi::QueueFamily queue_family = {};
};

struct CommandPool {
  rhi::CommandPool handle = {};
  rhi::QueueFamily queue_family = {};
};

} // namespace ren
