#pragma once
#include "Std.h"

namespace ren::sh {

static const uint SPD_THREADS_X = 16;
static const uint SPD_THREADS_Y = 16;

static const uint SPD_THREAD_ELEMS_X = 4;
static const uint SPD_THREAD_ELEMS_Y = 4;

static const uint SPD_TILE_SIZE_X = SPD_THREADS_X * SPD_THREAD_ELEMS_X;
static const uint SPD_TILE_SIZE_Y = SPD_THREADS_Y * SPD_THREAD_ELEMS_Y;
static_assert(SPD_TILE_SIZE_X == SPD_TILE_SIZE_Y);
static const uint SPD_TILE_SIZE = SPD_TILE_SIZE_X;
static_assert((SPD_TILE_SIZE & (SPD_TILE_SIZE - 1)) == 0);

static const uint SPD_NUM_TILE_MIPS = 7;

static_assert((1 << (SPD_NUM_TILE_MIPS - 1)) == SPD_TILE_SIZE);

static const uint SPD_MAX_SIZE = SPD_TILE_SIZE * SPD_TILE_SIZE;
static const uint SPD_MAX_NUM_MIPS = 2 * SPD_NUM_TILE_MIPS - 1;
static_assert((1 << (SPD_MAX_NUM_MIPS - 1)) == SPD_MAX_SIZE);

} // namespace ren::sh
