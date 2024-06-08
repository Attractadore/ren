#include "OpaquePass.glsl"

layout(location = V_POSITION) out vec3 v_position;

layout(location = V_NORMAL) out vec3 v_normal;
layout(location = V_TANGENT) out vec4 v_tangent;

layout(location = V_UV) out vec2 v_uv;

layout(location = V_COLOR) out vec4 v_color;

layout(location = V_MATERIAL) out flat uint v_material;

void main() {
  OpaquePassUniforms ub = DEREF(pc.ub);
  MeshInstance mesh_instance = DEREF(ub.mesh_instances[gl_BaseInstance]);
  Mesh mesh = DEREF(ub.meshes[mesh_instance.mesh]);
  v_material = mesh_instance.material;

  mat4x3 transform_matrix = DEREF(ub.transform_matrices[gl_BaseInstance]);
  mat3 normal_matrix = DEREF(ub.normal_matrices[gl_BaseInstance]);

  vec3 position = decode_position(DEREF(mesh.positions[gl_VertexIndex]));

  position = transform_matrix * vec4(position, 1.0f);
  v_position = position;
  gl_Position = ub.proj_view * vec4(position, 1.0f);

  vec3 normal = decode_normal(DEREF(mesh.normals[gl_VertexIndex]));
  if (OPAQUE_FEATURE_TS) {
    vec4 tangent = decode_tangent(DEREF(mesh.tangents[gl_VertexIndex]), normal);
    normal = normalize(normal_matrix * normal);
    tangent.xyz = normalize(transform_matrix * vec4(tangent.xyz, 0.0f));
    v_tangent = tangent;
  } else {
    normal = normalize(normal_matrix * normal);
  }
  v_normal = normal;

  if (OPAQUE_FEATURE_UV) {
    v_uv = decode_uv(DEREF(mesh.uvs[gl_VertexIndex]), mesh.uv_bs);
  }

  if (OPAQUE_FEATURE_VC) {
    v_color = decode_color(DEREF(mesh.colors[gl_VertexIndex]));
  }
}
