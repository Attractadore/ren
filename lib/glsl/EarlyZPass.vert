#include "EarlyZPass.h"

PUSH_CONSTANTS(EarlyZPassConstants);

void main() {
  MeshInstance mesh_instance = pc.mesh_instances[gl_BaseInstance].mesh_instance;
  Mesh mesh = pc.meshes[mesh_instance.mesh].mesh;
  mat4x3 transform_matrix = pc.transform_matrices[gl_BaseInstance].matrix;
  vec3 position = decode_position(mesh.positions[gl_VertexIndex].position);
  position = transform_matrix * vec4(position, 1.0f);
  gl_Position = pc.proj_view * vec4(position, 1.0f);
}
