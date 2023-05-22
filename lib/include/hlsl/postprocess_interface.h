#pragma once
#include "cpp.h"

REN_NAMESPACE_BEGIN

struct ReinhardPushConstants {
  uint64_t exposure_ptr;
  uint tex;
};

constexpr uint REINHARD_THREADS_X = 8;
constexpr uint REINHARD_THREADS_Y = 8;

REN_NAMESPACE_END
