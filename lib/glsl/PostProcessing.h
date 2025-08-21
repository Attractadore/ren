#pragma once
#include "DevicePtr.h"
#include "LuminanceHistogram.h"
#include "Std.h"
#include "Texture.h"

GLSL_NAMESPACE_BEGIN

const uint TONE_MAPPER_LINEAR = 0;
const uint TONE_MAPPER_REINHARD = 1;
const uint TONE_MAPPER_ACES = 2;
const uint TONE_MAPPER_KHR_PBR_NEUTRAL = 3;

#if __cplusplus

enum class ToneMapper {
  Linear = TONE_MAPPER_LINEAR,
  Reinhard = TONE_MAPPER_REINHARD,
  Aces = TONE_MAPPER_ACES,
  KhrPbrNeutral = TONE_MAPPER_KHR_PBR_NEUTRAL,
  Count,
};

#endif

const uint COLOR_SPACE_SRGB = 0;

#if __cplusplus

enum class ColorSpace {
  Srgb = COLOR_SPACE_SRGB,
};

#endif

GLSL_PUSH_CONSTANTS PostProcessingArgs {
  GLSL_PTR(LuminanceHistogram) histogram;
  GLSL_READONLY GLSL_PTR(float) exposure;
  Texture2D hdr;
  StorageTexture2D sdr;
  GLSL_ENUM(ToneMapper) tone_mapper;
  GLSL_ENUM(ColorSpace) output_color_space;
}
GLSL_PC;

GLSL_NAMESPACE_END
