#include "EarlyZPass.h"

PUSH_CONSTANTS(EarlyZPassArgs);

void main() {
  MeshInstance mesh_instance = DEREF(pc.mesh_instances[gl_BaseInstance]);
  mat4x3 transform_matrix = DEREF(pc.transform_matrices[gl_BaseInstance]);

  Mesh mesh = DEREF(pc.meshes[mesh_instance.mesh]);

  uint vertex = DEREF(mesh.meshlet_indices[gl_VertexIndex]);

  vec3 position = decode_position(DEREF(mesh.positions[vertex]));

  position = transform_matrix * vec4(position, 1.0f);
  gl_Position = pc.proj_view * vec4(position, 1.0f);
}
