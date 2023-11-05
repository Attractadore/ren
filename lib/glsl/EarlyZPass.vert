#include "EarlyZPass.glsl"

PUSH_CONSTANTS GLSL_EARLY_Z_CONSTANTS pc;

void main() {
  mat4x3 transform_matrix = pc.transform_matrices[gl_BaseInstance].matrix;
  vec3 position = decode_position(pc.positions[gl_VertexIndex].position);
  position = transform_matrix * vec4(position, 1.0f);
  gl_Position = pc.pv * vec4(position, 1.0f);
}
