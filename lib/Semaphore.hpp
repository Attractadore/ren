#pragma once
#include "ren/core/GenIndex.hpp"
#include "ren/core/StdDef.hpp"
#include "ren/core/String.hpp"
#include "rhi.hpp"

namespace ren {

struct SemaphoreCreateInfo {
  String8 name = "Semaphore";
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
