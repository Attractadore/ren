#ifndef REN_GLSL_STREAM_SCAN_H
#define REN_GLSL_STREAM_SCAN_H

#include "Common.h"
#include "DevicePtr.h"

GLSL_NAMESPACE_BEGIN

const uint SCAN_BLOCK_SIZE = 128;
const uint SCAN_THREAD_ELEMS = 1;
const uint SCAN_BLOCK_ELEMS = SCAN_BLOCK_SIZE * SCAN_THREAD_ELEMS;

const uint SCAN_TYPE_EXCLUSIVE = 0;
const uint SCAN_TYPE_INCLUSIVE = 1;

inline uint get_stream_scan_block_sum_count(uint count) {
  uint num_groups = (count + SCAN_BLOCK_ELEMS - 1) / SCAN_BLOCK_ELEMS;
  return num_groups + 1;
}

struct StreamScanArgs {
  GLSL_PTR(void) src;
  GLSL_PTR(void) block_sums;
  GLSL_PTR(void) dst;
  GLSL_PTR(uint) num_started;
  GLSL_PTR(uint) num_finished;
  uint count;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_STREAM_SCAN_H
