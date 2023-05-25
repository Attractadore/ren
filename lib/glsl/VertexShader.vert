#include "color_interface.glsl"

UBO(GLOBAL_SET, GLOBAL_DATA_SLOT) {
  GlobalData g_global;
};

SSBO(GLOBAL_SET, MODEL_MATRICES_SLOT, restrict readonly) {
  mat4x3 g_transform_matrices[];
};

SSBO(GLOBAL_SET, NORMAL_MATRICES_SLOT, restrict readonly) {
  mat3 g_normal_matrices[];
};

PUSH_CONSTANTS { ColorPushConstants g_pcs; };

OUT(POSITION_LOCATION) vec3 out_position;
OUT(COLOR_LOCATION) vec4 out_color;
OUT(NORMAL_LOCATION) vec3 out_normal;
OUT(UV_LOCATION) vec2 out_uv;

void main() {
  uint index = gl_VertexIndex;
  uint matrix_index = g_pcs.matrix_index;

  mat4x3 transform_mat = g_transform_matrices[matrix_index];
  mat3 normal_mat = g_normal_matrices[matrix_index];
  mat4 prov_view_mat = g_global.proj_view;

  vec3 position = g_pcs.positions_ptr.positions[index];
  position = transform_mat * vec4(position, 1.0f);

  vec4 color = vec4(1.0f);
  if (!IS_NULL(g_pcs.colors_ptr)) {
    color.xyz = decode_color(g_pcs.colors_ptr.colors[index]);
  }

  vec3 normal = decode_normal(g_pcs.normals_ptr.normals[index]);
  normal = normal_mat * normal;

  vec2 uv = vec2(0.0f);
  if (!IS_NULL(g_pcs.uvs_ptr)) {
    uv = g_pcs.uvs_ptr.uvs[index];
  }

  gl_Position = prov_view_mat * vec4(position, 1.0f);
  out_position = position;
  out_color = color;
  out_normal = normal;
  out_uv = uv;
}
