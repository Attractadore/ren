#ifndef REN_GLSL_VERTEX_H
#define REN_GLSL_VERTEX_H

#include "common.h"

GLSL_NAMESPACE_BEGIN

struct Position {
  i16vec3 position;
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

GLSL_NAMESPACE_END

#endif // REN_GLSL_VERTEX_H
