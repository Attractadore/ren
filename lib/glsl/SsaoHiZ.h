#pragma once
#include "Array.h"
#include "DevicePtr.h"
#include "Spd.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

GLSL_PUSH_CONSTANTS LinearHiZArgs {
  GLSL_PTR(uint) spd_counter;
  uint num_mips;
  GLSL_ARRAY(StorageTexture2D, dsts, SPD_MAX_NUM_MIPS);
  SampledTexture2D src;
  float znear;
}
GLSL_PC;

GLSL_NAMESPACE_END
