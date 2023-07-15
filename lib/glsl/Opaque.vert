#include "OpaquePass.glsl"

PUSH_CONSTANTS { OpaqueConstants g_pcs; };

OUT_BLOCK VS_OUT g_out;

void main() {
  uint index = gl_VertexIndex;
  uint matrix_index = g_pcs.matrix;

  restrict readonly OpaqueUniformBuffer ub = g_pcs.ub;

  mat4x3 transform_matrix = ub.transform_matrices[matrix_index].matrix;
  mat3 normal_matrix = ub.normal_matrices[matrix_index].matrix;

  vec3 position = g_pcs.positions[index].position;
  position = transform_matrix * vec4(position, 1.0f);

  vec4 color = vec4(1.0f);
  if (!IS_NULL(g_pcs.colors)) {
    color.xyz = decode_color(g_pcs.colors[index].color);
  }

  vec3 normal = decode_normal(g_pcs.normals[index].normal);
  normal = normal_matrix * normal;

  vec2 uv = vec2(0.0f);
  if (!IS_NULL(g_pcs.uvs)) {
    uv = g_pcs.uvs[index].uv;
  }

  gl_Position = ub.pv * vec4(position, 1.0f);
  g_out.position = position;
  g_out.color = color;
  g_out.normal = normal;
  g_out.uv = uv;
}
