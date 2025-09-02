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

static const float DEFAULT_MIDDLE_GRAY = 0.127f;

inline float manual_exposure(float stops, float ec, float middle_gray) {
  return middle_gray * exp2(stops - ec);
}

// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// Page 85
inline float camera_exposure(float aperture, float inv_shutter_time, float iso,
                             float ec, float middle_gray) {
  float ev100_pow2 = aperture * aperture * inv_shutter_time * 100.0f / iso;
  float max_luminance = 1.2f * ev100_pow2 * exp2(-ec);
  return middle_gray / (DEFAULT_MIDDLE_GRAY * max_luminance);
};

// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// Page 85
inline float automatic_exposure(float log_luminance, float ec,
                                float middle_gray) {
  float luminance = exp2(log_luminance - ec);
  float max_luminance = 9.6f * luminance;
  return middle_gray / (DEFAULT_MIDDLE_GRAY * max_luminance);
}

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
