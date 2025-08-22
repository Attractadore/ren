#include "Opaque.h"
#include "Transforms.h"

namespace ren::sh {

[[vk::push_constant]] OpaqueArgs pc;

OpaqueVsOutput main() {
  OpaqueVsOutput out;

  MeshInstance mesh_instance = pc.mesh_instances[gl_BaseInstance];
  mat4x3 transform_matrix = pc.transform_matrices[gl_BaseInstance];
  mat3 normal_matrix = normal(mat3(transform_matrix));

  Mesh mesh = pc.meshes[mesh_instance.mesh];

  uint vertex = mesh.meshlet_indices[gl_VertexIndex];

  vec3 position = decode_position(mesh.positions[vertex]);

  position = transform_matrix * vec4(position, 1.0f);
  out.sv_position = pc.proj_view * vec4(position, 1.0f);
  out.position = position;

  vec3 normal = decode_normal(mesh.normals[vertex]);
  if (OPAQUE_FEATURE_TS) {
    vec4 tangent = decode_tangent(mesh.tangents[vertex], normal);
    normal = normalize(normal_matrix * normal);
    tangent.xyz = normalize(transform_matrix * vec4(tangent.xyz, 0.0f));
    out.tangent = tangent;
  } else {
    normal = normalize(normal_matrix * normal);
  }
  out.normal = normal;

  if (OPAQUE_FEATURE_UV) {
    out.uv = decode_uv(mesh.uvs[vertex], mesh.uv_bs);
  }

  if (OPAQUE_FEATURE_VC) {
    out.color = decode_color(mesh.colors[vertex]);
  }

  out.material = mesh_instance.material;

  return out;
}

}
