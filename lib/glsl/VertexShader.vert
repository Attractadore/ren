#include "color_interface.glsl"

PUSH_CONSTANTS { ColorConstants g_pcs; };

OUT(POSITION_LOCATION) vec3 out_position;
OUT(COLOR_LOCATION) vec4 out_color;
OUT(NORMAL_LOCATION) vec3 out_normal;
OUT(UV_LOCATION) vec2 out_uv;

void main() {
  uint index = gl_VertexIndex;
  uint matrix_index = g_pcs.matrix_index;

  ColorUB ub = g_pcs.ub_ptr;

  mat4x3 transform_matrix = ub.transform_matrices_ptr[matrix_index].matrix;
  mat3 normal_matrix = ub.normal_matrices_ptr[matrix_index].matrix;

  vec3 position = g_pcs.positions_ptr[index].position;
  position = transform_matrix * vec4(position, 1.0f);

  vec4 color = vec4(1.0f);
  if (!IS_NULL(g_pcs.colors_ptr)) {
    color.xyz = decode_color(g_pcs.colors_ptr[index].color);
  }

  vec3 normal = decode_normal(g_pcs.normals_ptr[index].normal);
  normal = normal_matrix * normal;

  vec2 uv = vec2(0.0f);
  if (!IS_NULL(g_pcs.uvs_ptr)) {
    uv = g_pcs.uvs_ptr[index].uv;
  }

  gl_Position = ub.proj_view * vec4(position, 1.0f);
  out_position = position;
  out_color = color;
  out_normal = normal;
  out_uv = uv;
}
