#pragma once
#include "Std.h"

namespace ren::sh {

inline mat3 adjugate(mat3 m) {
  return mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
}

inline mat3 normal(mat3 m) { return adjugate(m); }

inline vec2 pixel_view_space_size(float rcp_p00, float rcp_p11, vec2 rcp_size,
                                  float z) {
  // s_ndc = a * s_v / -z_v =>
  // s_v = s_ndc * -z_v / a
  // s_ndc = 2 * s_uv = 2 / size
  return (2.0f * -z) * vec2(rcp_p00, rcp_p11) * rcp_size;
}

inline vec3 normal_offset(vec3 p, vec3 v, vec3 n, vec2 pixel_size) {
  float diag = length(pixel_size);
  float cos_v = dot(v, n);
  float sin_v = sqrt(1.0f - cos_v * cos_v);
  return p + (0.5f * sin_v * diag) * n;
}

inline vec2 ndc_to_uv(vec2 ndc) {
  return vec2(0.5f + 0.5f * ndc.x, 0.5f - 0.5f * ndc.y);
}

inline vec2 uv_to_ndc(vec2 uv) {
  return vec2(2.0f * uv.x - 1.0f, 1.0f - 2.0f * uv.y);
}

inline vec3 view_to_ndc(float p00, float p11, float znear, vec3 p) {
  return vec3(p.x * p00, p.y * p11, znear) / -p.z;
}

inline vec3 ndc_to_view(float rcp_p00, float rcp_p11, float znear, vec3 p) {
  float z = -znear / p.z;
  return vec3(p.x * rcp_p00 * -z, p.y * rcp_p11 * -z, z);
}

inline uvec2 linear_to_morton_2d(uint i) {
  uint x = i & 0x55555555;
  uint y = (i >> 1) & 0x55555555;
  // All bits in x and  y need to be shifted by a combination of shifts of
  // lengths 8, 4, 2 and 1.
  // Bit i needs to be shifted by i / 2.
  uvec2 m = uvec2(x, y);
  m = (m | (m >> uvec2(1))) & uvec2(0x33333333);
  m = (m | (m >> uvec2(2))) & uvec2(0x0F0F0F0F);
  m = (m | (m >> uvec2(4))) & uvec2(0x00FF00FF);
  m = (m | (m >> uvec2(8))) & uvec2(0x0000FFFF);
  return m;
}

inline float pack_depth_linear_16bit(float d, float znear) {
  return znear * (1.0f - d) / d;
}

inline float pack_z_linear_16bit(float z, float znear) { return z - znear; }

inline float unpack_z_linear_16bit(float z, float znear) { return z + znear; }

inline uvec2 hilbert_rotate(uint n, uint x, uint y, bool rx, bool ry) {
  if (!ry) {
    if (rx) {
      x = n - 1 - x;
      y = n - 1 - y;
    }
    return uvec2(y, x);
  }
  return uvec2(x, y);
}

inline uint hilbert_from_2d(uint n, uint x, uint y) {
  uint d = 0;
  for (uint s = n / 2; s > 0; s /= 2) {
    bool rx = (x & s) > 0;
    bool ry = (y & s) > 0;
    d += s * s * ((3 * uint(rx)) ^ uint(ry));
    uvec2 xy = hilbert_rotate(n, x, y, rx, ry);
    x = xy.x;
    y = xy.y;
  }
  return d;
}

inline vec3 linear_to_srgb(vec3 color) {
  return mix(color * 12.92f, 1.055f * pow(color, vec3(1.0f / 2.4f)) - 0.055f,
             greaterThanEqual(color, vec3(0.0031308f)));
}

inline float linear_to_srgb(float x) {
  return x >= 0.0031308f ? 1.055f * pow(x, 1.0f / 2.4f) - 0.055f : x * 12.92f;
}

inline float srgb_to_linear(float x) {
  return x > float(0.04045f) ? pow((x + 0.055f) / 1.055f, 2.4f) : x / 12.92f;
}

inline vec3 srgb_to_linear(vec3 color) {
  return mix(color / 12.92f, pow((color + 0.055f) / 1.055f, vec3(2.4f)),
             greaterThan(color, vec3(0.04045f)));
}

inline float color_to_luminance(vec3 color) {
  return dot(color, vec3(0.2126f, 0.7152f, 0.0722f));
}

inline uvec2 linear_to_local_2d(const uvec3 WG_SIZE, uint index) {
  const uint NUM_QUADS_X = WG_SIZE.x / 2;
  uint quad_index = index / 4;
  uvec2 quad_id = uvec2(quad_index % NUM_QUADS_X, quad_index / NUM_QUADS_X);
  uint quad_invocation_index = index % 4;
  uvec2 quad_invocation_id =
      uvec2(quad_invocation_index % 2, quad_invocation_index / 2);
  return 2u * quad_id + quad_invocation_id;
}

inline uvec2 linear_to_global_2d(const uvec3 WG_ID, const uvec3 WG_SIZE,
                                 uint index) {
  return uvec2(WG_ID) * uvec2(WG_SIZE) + linear_to_local_2d(WG_SIZE, index);
}

inline vec3 cube_map_face_pos_to_direction(uvec2 pos, uint face, uvec2 size) {
  // uv_face = 0.5 * (uv_c / |r| + 1) =>
  // uv_c = (2 * uv_face - 1) * |r|
  vec2 uv_face = (vec2(pos) + 0.5f) / vec2(size);
  vec2 uv_c = 2.0f * uv_face - 1.0f;
  float r_c = (face % 2 == 0) ? 1.0f : -1.0f;

  vec3 v;
  if (face / 2 == 0) {
    v = vec3(r_c, -uv_c.y, r_c * -uv_c.x);
  } else if (face / 2 == 1) {
    v = vec3(uv_c.x, r_c, r_c * uv_c.y);
  } else {
    v = vec3(r_c * uv_c.x, -uv_c.y, r_c);
  }

  return v;
}

inline vec2 direction_to_equirectangular_uv(vec3 r) {
  float phi = atan(r.y, r.x);
  float theta = acos(r.z / length(r));
  return vec2(phi / TWO_PI, theta / PI);
}

inline float reduce_quad_checkered_min_max(uint x, uint y, vec4 v) {
  float minv = min(min(v.x, v.y), min(v.z, v.w));
  float maxv = max(max(v.x, v.y), max(v.z, v.w));
  return (x + y) % 2 == 0 ? minv : maxv;
}

inline vec3 make_orthogonal_vector(vec3 v) {
  if (abs(v.y) > abs(v.z)) {
    return vec3(v.y, -v.x, 0.0f);
  }
  return vec3(v.z, 0.0f, -v.x);
}

inline uint pack_r10g10b10a2_unorm(vec3 color) {
  uint r = uint(round(clamp(color.r, 0.0f, 1.0f) * 1023.0f));
  uint g = uint(round(clamp(color.g, 0.0f, 1.0f) * 1023.0f));
  uint b = uint(round(clamp(color.b, 0.0f, 1.0f) * 1023.0f));
  uint a = 3;
  return (a << 30) | (b << 20) | (g << 10) | r;
}

inline vec4 unpack_r10g10b10a2_unorm(uint bits) {
  uint r = (bits >> 0) & 1023;
  uint g = (bits >> 10) & 1023;
  uint b = (bits >> 20) & 1023;
  uint a = bits >> 30;
  vec4 color;
  color.r = r / 1023.0f;
  color.g = g / 1023.0f;
  color.b = b / 1023.0f;
  color.a = a / 3.0f;
  return color;
}

} // namespace ren::sh
