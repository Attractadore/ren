#pragma once
#include "LuminanceHistogram.h"
#include "Std.h"

namespace ren::sh {

enum ExposureMode {
  EXPOSURE_MODE_MANUAL,
  EXPOSURE_MODE_CAMERA,
  EXPOSURE_MODE_AUTOMATIC,
  EXPOSURE_MODE_COUNT,
};

enum ToneMapper {
  TONE_MAPPER_LINEAR,
  TONE_MAPPER_REINHARD,
  TONE_MAPPER_LUMINANCE_REINHARD,
  TONE_MAPPER_ACES,
  TONE_MAPPER_KHR_PBR_NEUTRAL,
  TONE_MAPPER_AGX_DEFAULT,
  TONE_MAPPER_AGX_GOLDEN,
  TONE_MAPPER_AGX_PUNCHY,
  TONE_MAPPER_COUNT
};

enum ColorSpace {
  COLOR_SPACE_SRGB,
};

struct PostProcessingArgs {
  DevicePtr<LuminanceHistogram> histogram;
  DevicePtr<float> exposure;
  Handle<Texture2D> hdr;
  Handle<RWTexture2D> sdr;
  ToneMapper tone_mapper;
  ColorSpace output_color_space;
};

} // namespace ren::sh
