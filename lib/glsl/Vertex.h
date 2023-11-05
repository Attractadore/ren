#ifndef REN_GLSL_VERTEX_H
#define REN_GLSL_VERTEX_H

#include "common.h"

GLSL_NAMESPACE_BEGIN

struct Position {
  int16_t x, y, z;
};

inline Position encode_position(vec3 position, vec3 bb) {
  vec3 scale = float(1 << 15) / bb;
  ivec3 iposition = min(ivec3(round(position * scale)), (1 << 15) - 1);
  Position eposition;
  eposition.x = int16_t(iposition.x);
  eposition.y = int16_t(iposition.y);
  eposition.z = int16_t(iposition.z);
  return eposition;
}

inline vec3 decode_position(Position position) {
  return vec3(position.x, position.y, position.z);
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
