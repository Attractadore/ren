#pragma once
#include "Std.h"
#include "Transforms.h"

namespace ren::sh {

// Absolute threshold of vision is 1e-6 cd/m^2
static const float MIN_LUMINANCE = 1.0e-7f;
// Eye damage is possible at 1e8 cd/m^2
static const float MAX_LUMINANCE = 1.0e9f;

static const float MIN_LOG_LUMINANCE = log2(MIN_LUMINANCE);
static const float MAX_LOG_LUMINANCE = log2(MAX_LUMINANCE);

static const uint NUM_LUMINANCE_HISTOGRAM_BINS = 64;

static const float DEFAULT_MIDDLE_GRAY = 0.127f;

inline float manual_exposure(float stops, float ec) { return exp2(stops - ec); }

// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// Page 85
inline float camera_exposure(float aperture, float inv_shutter_time, float iso,
                             float ec) {
  float ev100_pow2 = aperture * aperture * inv_shutter_time * 100.0f / iso;
  float max_luminance = 1.2f * ev100_pow2 * exp2(-ec);
  return 1.0f / (DEFAULT_MIDDLE_GRAY * max_luminance);
};

// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// Page 85
inline float automatic_exposure(float log_luminance, float ec) {
  float luminance = exp2(log_luminance - ec);
  float max_luminance = 9.6f * luminance;
  return 1.0f / (DEFAULT_MIDDLE_GRAY * max_luminance);
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
  TONE_MAPPER_AGX_PUNCHY,
  TONE_MAPPER_COUNT
};

inline vec3 tone_map_reinhard(vec3 color) { return color / (1.0f + color); }

inline vec3 tone_map_luminance_reinhard(vec3 color) {
  float luminance = color_to_luminance(color);
  return 1.0f / (1.0f + luminance) * color;
}

inline float tone_map_reinhard(float x) { return x / (1.0f + x); }

// https://www.desmos.com/calculator/vyk84noijd
inline float inverse_tone_map_reinhard(float y) { return y / (1.0f - y); }

inline vec3 aces_rrt_and_odt_fit(vec3 x) {
  vec3 a = x * (x + 0.0245786f) - 0.000090537f;
  vec3 b = x * (0.983729f * x + 0.4329510f) + 0.238081f;
  return a / b;
}

inline float aces_rrt_and_odt_fit(float x) {
  float a = x * (x + 0.0245786f) - 0.000090537f;
  float b = x * (0.983729f * x + 0.4329510f) + 0.238081f;
  return a / b;
}

// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
inline vec3 tone_map_aces(vec3 color) {
  // clang-format off
  const mat3 ACES_INPUT_MATRIX = mat3(
    0.59719f, 0.07600f, 0.02840f,
    0.35458f, 0.90834f, 0.13383f,
    0.04823f, 0.01566f, 0.83777f
  );

  const mat3 ACES_OUTPUT_MATRIX = mat3(
    1.60475f, -0.10208f, -0.00327f, 
    -0.53108f, 1.10813f, -0.07276f,
    -0.07367f, -0.00605f, 1.07602f
  );
  // clang-format on

  color = ACES_INPUT_MATRIX * color;
  color = aces_rrt_and_odt_fit(color);
  color = ACES_OUTPUT_MATRIX * color;

  return clamp(color, 0.0f, 1.0f);
}

inline float tone_map_aces(float x) {
  x = aces_rrt_and_odt_fit(x);
  return clamp(x, 0.0f, 1.0f);
}

// https://www.wolframalpha.com/input?i=solve+y+%3D+%28x%5E2+%2B+ax+%2B+b%29+%2F+%28cx%5E2+%2B+dx+%2B+f%29+for+x
// https://www.desmos.com/calculator/jsyck68fom
inline float inverse_aces_rrt_odt_fit(float y) {
  float a = 0.0245786f;
  float b = -0.000090537f;
  float c = 0.983729f;
  float d = 0.4329510f;
  float f = 0.238081f;
  float x = (a - d * y) * (a - d * y) - 4.0f * (1.0f - c * y) * (b - f * y);
  x = -sqrt(x) + a - d * y;
  x = 0.5f * x / (c * y - 1.0f);
  return x;
}

inline float inverse_tone_map_aces(float y) {
  return inverse_aces_rrt_odt_fit(y);
}

inline vec3 agx_default_contrast_curve(vec3 x) {
  vec3 x2 = x * x;
  vec3 x3 = x2 * x;
  vec3 x4 = x2 * x2;
  vec3 x5 = x4 * x;
  vec3 x6 = x4 * x2;
  vec3 x7 = x4 * x3;
  return -17.86f * x7 + 78.01f * x6 - 126.7f * x5 + 92.06f * x4 - 28.72f * x3 +
         4.361f * x2 - 0.1718f * x + 0.002857f;
}

inline float agx_default_contrast_curve(float x) {
  float x2 = x * x;
  float x3 = x2 * x;
  float x4 = x2 * x2;
  float x5 = x4 * x;
  float x6 = x4 * x2;
  float x7 = x4 * x3;
  return -17.86f * x7 + 78.01f * x6 - 126.7f * x5 + 92.06f * x4 - 28.72f * x3 +
         4.361f * x2 - 0.1718f * x + 0.002857f;
}

static const float AGX_MIN_EV = -12.47393f;
static const float AGX_MAX_EV = 4.026069f;

// https://iolite-engine.com/blog_posts/minimal_agx_implementation
inline vec3 tone_map_agx(vec3 color, bool punchy) {
  // clang-format off
  const mat3 AGX_INPUT_MATRIX = mat3(
    0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
    0.0784335999999992f, 0.878468636469772f, 0.0784336f,
    0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f
  );

  const mat3 AGX_OUTPUT_MATRIX = mat3(
    1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
    -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
    -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f
  );
  // clang-format on

  color = AGX_INPUT_MATRIX * color;
  color = clamp(log2(color), AGX_MIN_EV, AGX_MAX_EV);
  color = (color - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV);
  color = agx_default_contrast_curve(color);

  if (punchy) {
    float power = 1.35f;
    float sat = 1.4f;
    color = pow(color, vec3(power));
    float luminance = color_to_luminance(color);
    color = luminance + sat * (color - luminance);
  }

  color = AGX_OUTPUT_MATRIX * color;
  return srgb_to_linear(color);
}

inline float tone_map_agx(float x, bool punchy) {
  x = clamp(log2(x), AGX_MIN_EV, AGX_MAX_EV);
  x = (x - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV);
  x = agx_default_contrast_curve(x);
  float power = punchy ? 1.35f : 1.0f;
  x = pow(x, 2.2f * power);
  return x;
}

inline float inverse_agx_default_contrast_curve(float y) {
  float y2 = y * y;
  float y3 = y2 * y;
  float y4 = y2 * y2;
  float y5 = y4 * y;
  float y6 = y4 * y2;
  float y7 = y4 * y3;
  float y8 = y4 * y4;
  float y9 = y8 * y;
  return 1361.59563847 * y9 - 6272.21061217 * y8 + 12229.6903101 * y7 -
         13136.56600774 * y6 + 8479.4059117 * y5 - 3369.19675743 * y4 +
         813.47198704 * y3 - 114.44573763 * y2 + 9.31767389 * y + 0.02343653;
}

// https://www.desmos.com/calculator/anyjp58g5g
inline float inverse_tone_map_agx(float y, bool punchy) {
  float power = punchy ? 1.35f : 1.0f;
  y = pow(y, 1.0f / (2.2f * power));
  y = inverse_agx_default_contrast_curve(y);
  y = (AGX_MAX_EV - AGX_MIN_EV) * y + AGX_MIN_EV;
  y = exp2(y);
  return y;
}

// https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral
inline vec3 tone_map_khr_pbr_neutral(vec3 color) {
  const float F0 = 0.04f;
  const float KS = 0.80f - F0;
  const float KD = 0.15f;

  float x = min(color.r, min(color.g, color.b));
  float f = x <= 2.0f * F0 ? x - x * x / (4.0f * F0) : F0;
  float p = max(color.r, max(color.g, color.b)) - f;
  if (p <= KS) {
    return color - f;
  }

  float p_n = 1.0f - (1.0f - KS) * (1.0f - KS) / (p + 1.0f - 2.0f * KS);
  float g = 1.0f / (KD * (p - p_n) + 1.0f);

  return mix(vec3(p_n), (color - f) * p_n / p, g);
}

inline float tone_map_khr_pbr_neutral(float x) {
  const float F0 = 0.04f;
  const float KS = 0.80f - F0;
  const float KD = 0.15f;
  float p = x <= 2.0f * F0 ? x * x / (4.0f * F0) : x - F0;
  if (p <= KS) {
    return p;
  }
  float p_n = 1.0f - (1.0f - KS) * (1.0f - KS) / (p + 1.0f - 2.0f * KS);
  return p_n;
}

// https://www.desmos.com/calculator/vt6dr9tb7b
inline float inverse_tone_map_khr_pbr_neutral(float y) {
  y = min(y, 0.99f);
  const float F0 = 0.04f;
  const float KS = 0.80f - F0;
  if (y > KS) {
    y = (1.0f - KS) * (1.0f - KS) / (1.0f - y + 0.0001f) - 1.0f + 2.0f * KS;
  }
  return y <= F0 ? sqrt(4.0f * F0 * y) : y + F0;
}

inline vec3 tone_map(vec3 color, ToneMapper tone_mapper) {
  switch (tone_mapper) {
  default:
    return color;
  case TONE_MAPPER_LINEAR:
    return color;
  case TONE_MAPPER_REINHARD:
    return tone_map_reinhard(color);
  case TONE_MAPPER_LUMINANCE_REINHARD:
    return tone_map_luminance_reinhard(color);
  case TONE_MAPPER_ACES:
    return tone_map_aces(color);
  case TONE_MAPPER_KHR_PBR_NEUTRAL:
    return tone_map_khr_pbr_neutral(color);
  case TONE_MAPPER_AGX_DEFAULT:
  case TONE_MAPPER_AGX_PUNCHY: {
    return tone_map_agx(color, tone_mapper == TONE_MAPPER_AGX_PUNCHY);
  }
  }
}

inline float tone_map(float x, ToneMapper tone_mapper) {
  switch (tone_mapper) {
  default:
    return x;
  case TONE_MAPPER_LINEAR:
    return x;
  case TONE_MAPPER_REINHARD:
  case TONE_MAPPER_LUMINANCE_REINHARD:
    return tone_map_reinhard(x);
  case TONE_MAPPER_ACES:
    return tone_map_aces(x);
  case TONE_MAPPER_KHR_PBR_NEUTRAL:
    return tone_map_khr_pbr_neutral(x);
  case TONE_MAPPER_AGX_DEFAULT:
  case TONE_MAPPER_AGX_PUNCHY: {
    return tone_map_agx(x, tone_mapper == TONE_MAPPER_AGX_PUNCHY);
  }
  }
}

inline float inverse_tone_map(float y, ToneMapper tone_mapper) {
  switch (tone_mapper) {
  default:
    return y;
  case TONE_MAPPER_LINEAR:
    return y;
  case TONE_MAPPER_REINHARD:
  case TONE_MAPPER_LUMINANCE_REINHARD:
    return inverse_tone_map_reinhard(y);
  case TONE_MAPPER_ACES:
    return inverse_tone_map_aces(y);
  case TONE_MAPPER_KHR_PBR_NEUTRAL:
    return inverse_tone_map_khr_pbr_neutral(y);
  case TONE_MAPPER_AGX_DEFAULT:
  case TONE_MAPPER_AGX_PUNCHY: {
    return inverse_tone_map_agx(y, tone_mapper == TONE_MAPPER_AGX_PUNCHY);
  }
  }
}

// https://gpuopen.com/download/GdcVdrLottes.pdf
inline vec3 dither_srgb(vec3 color, uint bit_depth, vec3 noise) {
  vec3 grain = 2.0f * noise - 1.0f;
  float step_size = 1.0f / ((1 << bit_depth) - 1);
  float black = 0.5f * srgb_to_linear(step_size);
  float biggest = 0.75f * (srgb_to_linear(1.0f + step_size) - 1.0f);
  return color + grain * min(color + black, biggest);
}

static const uint PP_HILBERT_CURVE_LEVEL = 6;
static const uint PP_HILBERT_CURVE_SIZE = 1 << PP_HILBERT_CURVE_LEVEL;

#if __SLANG__

inline vec3 dither_srgb(vec3 color, uint bit_depth, DevicePtr<vec3> noise,
                        uvec2 pos) {
  vec3 noise = noise[pos.y % PP_HILBERT_CURVE_SIZE * PP_HILBERT_CURVE_SIZE +
                     pos.x % PP_HILBERT_CURVE_SIZE];
  return dither_srgb(color, bit_depth, noise);
}

#endif

struct PostProcessingArgs {
  SH_RG_IGNORE(DevicePtr<vec3>) noise_lut;
  DevicePtr<float> luminance_histogram;
  DevicePtr<float> exposure;
  float middle_gray;
  MeteringMode metering_mode;
  float metering_pattern_relative_inner_size;
  float metering_pattern_relative_outer_size;
  Handle<Texture2D> hdr;
  Handle<RWTexture2D> sdr;
  ToneMapper tone_mapper;
  Handle<Sampler2D> ltm_llm;
  vec2 ltm_inv_size;
  int dithering;
};

} // namespace ren::sh
