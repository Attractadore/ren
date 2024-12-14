#include "Opaque.h"

layout(location = A_POSITION) out vec3 a_position;

layout(location = A_NORMAL) out vec3 a_normal;
layout(location = A_TANGENT) out vec4 a_tangent;

layout(location = A_UV) out vec2 a_uv;

layout(location = A_COLOR) out vec4 a_color;

layout(location = A_MATERIAL) out flat uint a_material;

void main() {
  MeshInstance mesh_instance = DEREF(pc.mesh_instances[gl_BaseInstance]);
  mat4x3 transform_matrix = DEREF(pc.transform_matrices[gl_BaseInstance]);
  mat3 normal_matrix = DEREF(pc.normal_matrices[gl_BaseInstance]);

  Mesh mesh = DEREF(pc.meshes[mesh_instance.mesh]);

  uint vertex = DEREF(mesh.meshlet_indices[gl_VertexIndex]);

  vec3 position = decode_position(DEREF(mesh.positions[vertex]));

  position = transform_matrix * vec4(position, 1.0f);
  a_position = position;
  gl_Position = pc.proj_view * vec4(position, 1.0f);

  vec3 normal = decode_normal(DEREF(mesh.normals[vertex]));
  if (OPAQUE_FEATURE_TS) {
    vec4 tangent = decode_tangent(DEREF(mesh.tangents[vertex]), normal);
    normal = normalize(normal_matrix * normal);
    tangent.xyz = normalize(transform_matrix * vec4(tangent.xyz, 0.0f));
    a_tangent = tangent;
  } else {
    normal = normalize(normal_matrix * normal);
  }
  a_normal = normal;

  if (OPAQUE_FEATURE_UV) {
    a_uv = decode_uv(DEREF(mesh.uvs[vertex]), mesh.uv_bs);
  }

  if (OPAQUE_FEATURE_VC) {
    a_color = decode_color(DEREF(mesh.colors[vertex]));
  }

  a_material = mesh_instance.material;
}
