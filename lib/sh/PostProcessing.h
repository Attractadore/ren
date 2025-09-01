#pragma once
#include "Std.h"

namespace ren::sh {

// Absolute threshold of vision is 1e-6 cd/m^2
static const float MIN_LUMINANCE = 1.0e-7f;
// Eye damage is possible at 1e8 cd/m^2
static const float MAX_LUMINANCE = 1.0e9f;

static const float MIN_LOG_LUMINANCE = log2(MIN_LUMINANCE);
static const float MAX_LOG_LUMINANCE = log2(MAX_LUMINANCE);

static const uint NUM_LUMINANCE_HISTOGRAM_BINS = 64;

enum ExposureMode {
  EXPOSURE_MODE_MANUAL,
  EXPOSURE_MODE_CAMERA,
  EXPOSURE_MODE_AUTOMATIC,
  EXPOSURE_MODE_COUNT,
};

enum MeteringMode {
  METERING_MODE_SPOT,
  METERING_MODE_CENTER_WEIGHTED,
  METERING_MODE_AVERAGE,
  METERING_MODE_COUNT,
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
  DevicePtr<float> luminance_histogram;
  DevicePtr<float> exposure;
  MeteringMode metering_mode;
  float metering_pattern_relative_inner_size;
  float metering_pattern_relative_outer_size;
  Handle<Texture2D> hdr;
  Handle<RWTexture2D> sdr;
  ToneMapper tone_mapper;
  ColorSpace output_color_space;
};

} // namespace ren::sh
