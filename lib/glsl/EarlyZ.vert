#include "EarlyZPass.glsl"

PUSH_CONSTANTS GLSL_EARLY_Z_CONSTANTS g_pcs;

void main() {
  mat4x3 transform_matrix = g_pcs.transform_matrices[gl_BaseInstance].matrix;
  vec3 position = g_pcs.positions[gl_VertexIndex].position;
  position = transform_matrix * vec4(position, 1.0f);
  gl_Position = g_pcs.pv * vec4(position, 1.0f);
}
