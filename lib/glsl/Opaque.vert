#include "OpaquePass.glsl"

layout(location = V_POSITION) out vec3 v_position;

layout(location = V_NORMAL) out vec3 v_normal;
layout(location = V_TANGENT) out vec4 v_tangent;

layout(location = V_UV) out vec2 v_uv;

layout(location = V_COLOR) out vec4 v_color;

layout(location = V_MATERIAL) out flat uint v_material;

void main() {
  MeshInstance mesh_instance = pc.ub.mesh_instances[gl_BaseInstance].mesh_instance;
  Mesh mesh = pc.ub.meshes[mesh_instance.mesh].mesh;
  v_material = mesh_instance.material;

  mat4x3 transform_matrix = pc.ub.transform_matrices[gl_BaseInstance].matrix;
  mat3 normal_matrix = pc.ub.normal_matrices[gl_BaseInstance].matrix;

  vec3 position = decode_position(mesh.positions[gl_VertexIndex].position);

  position = transform_matrix * vec4(position, 1.0f);
  v_position = position;
  gl_Position = pc.ub.proj_view * vec4(position, 1.0f);

  if (OPAQUE_FEATURE_TS) {
    vec3 normal = decode_normal(mesh.normals[gl_VertexIndex].normal);
    vec4 tangent = decode_tangent(mesh.tangents[gl_VertexIndex].tangent, normal);
    normal = normalize(normal_matrix * normal);
    tangent.xyz = normalize(transform_matrix * vec4(tangent.xyz, 0.0f));
    v_normal = normal;
    v_tangent = tangent;
  } else {
    vec3 normal = decode_normal(mesh.normals[gl_VertexIndex].normal);
    normal = normalize(normal_matrix * normal);
    v_normal = normal;
  }

  if (OPAQUE_FEATURE_UV) {
    v_uv = decode_uv(mesh.uvs[gl_VertexIndex].uv, mesh.uv_bs);
  }

  if (OPAQUE_FEATURE_VC) {
    v_color = decode_color(mesh.colors[gl_VertexIndex].color);
  }
}