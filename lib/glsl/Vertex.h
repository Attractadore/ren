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

GLSL_NAMESPACE_END

#endif // REN_GLSL_VERTEX_H
