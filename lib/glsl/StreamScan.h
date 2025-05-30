#pragma once
#include "DevicePtr.h"
#include "Math.h"
#include "Std.h"

GLSL_NAMESPACE_BEGIN

const uint SCAN_BLOCK_SIZE = 128;
const uint SCAN_THREAD_ELEMS = 1;
const uint SCAN_BLOCK_ELEMS = SCAN_BLOCK_SIZE * SCAN_THREAD_ELEMS;

const uint SCAN_TYPE_EXCLUSIVE = 0;
const uint SCAN_TYPE_INCLUSIVE = 1;

inline uint get_stream_scan_block_sum_count(uint count) {
  uint num_groups = ceil_div(count, SCAN_BLOCK_ELEMS);
  return num_groups + 1;
}

GLSL_PUSH_CONSTANTS StreamScanArgs {
  GLSL_PTR(void) src;
  GLSL_PTR(void) block_sums;
  GLSL_PTR(void) dst;
  GLSL_PTR(uint) num_started;
  GLSL_PTR(uint) num_finished;
  uint count;
}
GLSL_PC;

GLSL_NAMESPACE_END
