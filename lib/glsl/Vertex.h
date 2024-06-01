#ifndef REN_GLSL_VERTEX_H
#define REN_GLSL_VERTEX_H

#include "BufferReference.h"
#include "Common.h"

GLSL_NAMESPACE_BEGIN

struct BoundingSquare {
  vec2 min;
  vec2 max;
};

struct BoundingBox {
  vec3 min;
  vec3 max;
};

struct Position {
  i16vec3 position;
};

GLSL_REF_TYPE(2) PositionRef { Position position; };

struct PositionBoundingBox {
  Position min;
  Position max;
};

inline Position encode_position(vec3 position, vec3 bb) {
  vec3 scale = float(1 << 15) / bb;
  Position eposition;
  eposition.position =
      i16vec3(min(ivec3(round(position * scale)), (1 << 15) - 1));
  return eposition;
}

inline vec3 decode_position(Position position) {
  return vec3(position.position);
}

inline PositionBoundingBox encode_bounding_box(BoundingBox bb, vec3 ebb) {
  PositionBoundingBox pbb;
  pbb.min = encode_position(bb.min, ebb);
  pbb.max = encode_position(bb.max, ebb);
  return pbb;
}

inline BoundingBox decode_bounding_box(PositionBoundingBox pbb) {
  BoundingBox bb;
  bb.min = decode_position(pbb.min);
  bb.max = decode_position(pbb.max);
  return bb;
}

inline mat4 make_encode_position_matrix(vec3 bb) {
  vec3 scale = float(1 << 15) / bb;
  mat4 m = mat4(1.0f);
  m[0][0] = scale.x;
  m[1][1] = scale.y;
  m[2][2] = scale.z;
  return m;
}

inline mat4 make_decode_position_matrix(vec3 bb) {
  vec3 scale = bb / float(1 << 15);
  mat4 m = mat4(1.0f);
  m[0][0] = scale.x;
  m[1][1] = scale.y;
  m[2][2] = scale.z;
  return m;
}

struct Normal {
  u16vec2 normal;
};

GLSL_REF_TYPE(2) NormalRef { Normal normal; };

inline vec2 oct_wrap(vec2 v) {
  return (1.0f - abs(vec2(v.y, v.x))) *
         mix(vec2(-1.0f), vec2(1.0f), greaterThanEqual(v, vec2(0.0f)));
}

inline Normal encode_normal(vec3 normal) {
  normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
  vec2 xy = vec2(normal);
  xy = normal.z >= 0.0f ? xy : oct_wrap(xy);
  xy = xy * 0.5f + 0.5f;
  Normal enormal;
  enormal.normal =
      u16vec2(min(uvec2(round(xy * float(1 << 16))), (1u << 16) - 1));
  return enormal;
}

inline vec3 decode_normal(Normal normal) {
  vec2 xy = vec2(normal.normal) / float(1 << 16);
  xy = xy * 2.0f - 1.0f;
  float z = 1.0f - abs(xy.x) - abs(xy.y);
  xy = z >= 0.0f ? xy : oct_wrap(xy);
  return normalize(vec3(xy, z));
}

struct Tangent {
  uint16_t tangent_and_sign;
};

GLSL_REF_TYPE(2) TangentRef { Tangent tangent; };

inline vec3 ortho_vec(vec3 v) {
  if (abs(v.y) > abs(v.z)) {
    return vec3(v.y, -v.x, 0.0f);
  }
  return vec3(v.z, 0.0f, -v.x);
}

inline float sq_wrap(float v) {
  return (2.0f - abs(v)) * (v >= 0.0f ? 1.0f : -1.0f);
}

inline Tangent encode_tangent(vec4 tangent, vec3 normal) {
  vec3 t1 = normalize(ortho_vec(normal));
  vec3 t2 = cross(normal, t1);
  vec2 xy = vec2(dot(vec3(tangent), t1), dot(vec3(tangent), t2));
  float x = xy.x / (abs(xy.x) + abs(xy.y));
  x = xy.y >= 0.0f ? x : sq_wrap(x);
  x = x * 0.25f + 0.5f;
  uint tangent_and_sign = min(uint(round(x * float(1 << 15))), (1u << 15) - 1);
  tangent_and_sign |= (tangent.w < 0.0f) ? (1 << 15) : 0;
  Tangent etangent;
  etangent.tangent_and_sign = uint16_t(tangent_and_sign);
  return etangent;
}

inline vec4 decode_tangent(Tangent tangent, vec3 normal) {
  vec3 t1 = normalize(ortho_vec(normal));
  vec3 t2 = cross(normal, t1);
  uint tangent_and_sign = tangent.tangent_and_sign;
  float x = (tangent_and_sign & ((1 << 15) - 1)) / float(1 << 15);
  x = x * 4.0f - 2.0f;
  float y = 1.0f - abs(x);
  x = y >= 0.0f ? x : sq_wrap(x);
  vec2 xy = normalize(vec2(x, y));
  float sign = bool(tangent_and_sign & (1 << 15)) ? -1.0f : 1.0f;
  return vec4(t1 * xy.x + t2 * xy.y, sign);
}

struct alignas(4) UV {
  u16vec2 uv;
};

GLSL_REF_TYPE(4) UVRef { UV uv; };

inline UV encode_uv(vec2 uv, BoundingSquare bs) {
  vec2 fuv = float(1 << 16) * (uv - bs.min) / (bs.max - bs.min);
  fuv = clamp(round(fuv), 0.0f, float((1 << 16) - 1));
  UV euv;
  euv.uv = u16vec2(fuv);
  return euv;
}

inline vec2 decode_uv(UV uv, BoundingSquare bs) {
  return mix(bs.min, bs.max, vec2(uv.uv) / float(1 << 16));
}

struct alignas(4) Color {
  u8vec4 color;
};

GLSL_REF_TYPE(4) ColorRef { Color color; };

inline Color encode_color(vec4 color) {
  Color ecolor;
  ecolor.color = u8vec4(clamp(round(color * 255.0f), 0.0f, 255.0f));
  return ecolor;
}

inline vec4 decode_color(Color color) { return vec4(color.color) / 255.0f; }

GLSL_NAMESPACE_END

#endif // REN_GLSL_VERTEX_H
