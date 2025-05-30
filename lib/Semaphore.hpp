#pragma once
#include "DebugNames.hpp"
#include "core/GenIndex.hpp"
#include "core/StdDef.hpp"
#include "rhi.hpp"

namespace ren {

struct SemaphoreCreateInfo {
  REN_DEBUG_NAME_FIELD("Semaphore");
  rhi::SemaphoreType type = rhi::SemaphoreType::Timeline;
  u64 initial_value = 0;
};

struct Semaphore {
  rhi::Semaphore handle;
};

struct SemaphoreState {
  Handle<Semaphore> semaphore;
  u64 value = 0;
};

}; // namespace ren
