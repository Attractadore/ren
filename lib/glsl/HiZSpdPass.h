#ifndef REN_GLSL_HI_Z_SPD_PASS_H
#define REN_GLSL_HI_Z_SPD_PASS_H

#include "Common.h"
#include "DevicePtr.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

const uint HI_Z_SPD_THREADS_X = 16;
const uint HI_Z_SPD_THREADS_Y = 16;

const uint HI_Z_SPD_THREAD_ELEMS_X = 4;
const uint HI_Z_SPD_THREAD_ELEMS_Y = 4;

const uint HI_Z_SPD_TILE_SIZE_X = HI_Z_SPD_THREADS_X * HI_Z_SPD_THREAD_ELEMS_X;
const uint HI_Z_SPD_TILE_SIZE_Y = HI_Z_SPD_THREADS_Y * HI_Z_SPD_THREAD_ELEMS_Y;
static_assert(HI_Z_SPD_TILE_SIZE_X == HI_Z_SPD_TILE_SIZE_Y);
const uint HI_Z_SPD_TILE_SIZE = HI_Z_SPD_TILE_SIZE_X;
static_assert((HI_Z_SPD_TILE_SIZE & (HI_Z_SPD_TILE_SIZE - 1)) == 0);

const uint HI_Z_SPD_NUM_TILE_MIPS = 7;

static_assert((1 << (HI_Z_SPD_NUM_TILE_MIPS - 1)) == HI_Z_SPD_TILE_SIZE);

const uint HI_Z_SPD_MAX_SIZE = HI_Z_SPD_TILE_SIZE * HI_Z_SPD_TILE_SIZE;

struct HiZSpdPassArgs {
  /// SPD counter, initialize to 0.
  GLSL_PTR(uint) counter;
  /// Destination descriptors.
  GLSL_PTR(StorageTexture2D) dsts;
  /// Each destination side length must be the next smallest power-of-two after
  /// each source side's length.
  uvec2 dst_size;
  uint num_dst_mips;
  /// Source descriptor.
  SampledTexture2D src;
};

GLSL_NAMESPACE_END

#endif // REN_GLSL_HI_Z_SPD_PASS_H
