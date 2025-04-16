#pragma once
#include "Std.h"

GLSL_NAMESPACE_BEGIN

const uint SPD_THREADS_X = 16;
const uint SPD_THREADS_Y = 16;

const uint SPD_THREAD_ELEMS_X = 4;
const uint SPD_THREAD_ELEMS_Y = 4;

const uint SPD_TILE_SIZE_X = SPD_THREADS_X * SPD_THREAD_ELEMS_X;
const uint SPD_TILE_SIZE_Y = SPD_THREADS_Y * SPD_THREAD_ELEMS_Y;
static_assert(SPD_TILE_SIZE_X == SPD_TILE_SIZE_Y);
const uint SPD_TILE_SIZE = SPD_TILE_SIZE_X;
static_assert((SPD_TILE_SIZE & (SPD_TILE_SIZE - 1)) == 0);

const uint SPD_NUM_TILE_MIPS = 7;

static_assert((1 << (SPD_NUM_TILE_MIPS - 1)) == SPD_TILE_SIZE);

const uint SPD_MAX_SIZE = SPD_TILE_SIZE * SPD_TILE_SIZE;
const uint SPD_MAX_NUM_MIPS = 2 * SPD_NUM_TILE_MIPS - 1;
static_assert((1 << (SPD_MAX_NUM_MIPS - 1)) == SPD_MAX_SIZE);

GLSL_NAMESPACE_END
