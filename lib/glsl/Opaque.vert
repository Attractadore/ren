#include "OpaquePass.glsl"

PUSH_CONSTANTS GLSL_OPAQUE_CONSTANTS g_pcs;

OUT_BLOCK VS_OUT g_out;

void main() {
  const uint mesh_instance_index = gl_BaseInstance;
  MeshInstance mesh_instance = g_pcs.ub.mesh_instances[mesh_instance_index].mesh_instance;
  uint mesh_index = mesh_instance.mesh;
  Mesh mesh = g_pcs.ub.meshes[mesh_index].mesh;
  uint material_index = mesh_instance.mesh;

  uint vertex_offset = gl_VertexIndex - gl_BaseVertex;
  uint position_index = gl_VertexIndex;

  mat4x3 transform_matrix = g_pcs.ub.transform_matrices[mesh_instance_index].matrix;
  vec3 position = g_pcs.ub.positions[position_index].position;
  position = transform_matrix * vec4(position, 1.0f);

  mat3 normal_matrix = g_pcs.ub.normal_matrices[mesh_instance_index].matrix;
  vec3 normal = g_pcs.ub.normals[position_index].normal;
  normal = normal_matrix * normal;

  vec4 color = vec4(1.0f);
  if (mesh.base_color_vertex != MESH_ATTRIBUTE_UNUSED) {
    uint color_index = mesh.base_color_vertex + vertex_offset;
    // color = g_pcs.ub.colors[color_index].color;
  }

  vec2 uv = vec2(0.0f);
  if (mesh.base_uv_vertex != MESH_ATTRIBUTE_UNUSED) {
    uint uv_index = mesh.base_uv_vertex + vertex_offset;
    uv = g_pcs.ub.uvs[uv_index].uv;
  }

  gl_Position = g_pcs.ub.pv * vec4(position, 1.0f);
  g_out.position = position;
  g_out.normal = normal;
  g_out.color = color;
  g_out.uv = uv;
  g_out.material = material_index;
}
