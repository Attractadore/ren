#include "EarlyZ.h"

namespace ren::sh {

[[vk::push_constant]] EarlyZArgs pc;

vec4 main() : SV_Position {
  MeshInstance mesh_instance = pc.mesh_instances[gl_BaseInstance];
  mat4x3 transform_matrix = pc.transform_matrices[gl_BaseInstance];
  Mesh mesh = pc.meshes[mesh_instance.mesh];
  uint vertex = mesh.meshlet_indices[gl_VertexIndex];
  vec3 position = decode_position(mesh.positions[vertex]);
  position = transform_matrix * vec4(position, 1.0f);
  return pc.proj_view * vec4(position, 1.0f);
}

}
