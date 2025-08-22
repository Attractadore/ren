#pragma once
#include "Std.h"

namespace ren::sh {

static const uint SCAN_BLOCK_SIZE = 128;
static const uint SCAN_THREAD_ELEMS = 1;
static const uint SCAN_BLOCK_ELEMS = SCAN_BLOCK_SIZE * SCAN_THREAD_ELEMS;

inline uint get_stream_scan_block_sum_count(uint count) {
  uint num_groups = ceil_div(count, SCAN_BLOCK_ELEMS);
  return num_groups + 1;
}

struct StreamScanArgs {
  DevicePtr<uint> src;
  DevicePtr<uint> block_sums;
  DevicePtr<uint> dst;
  DevicePtr<uint> num_started;
  DevicePtr<uint> num_finished;
  uint count;
};

} // namespace ren::sh
