#include "OpaquePass.glsl"

layout(location = V_POSITION) out vec3 v_position;

layout(location = V_NORMAL) out vec3 v_normal;
layout(location = V_TANGENT) out vec3 v_tangent;
layout(location = V_BITANGENT) out vec3 v_bitangent;

layout(location = V_UV) out vec2 v_uv;

layout(location = V_COLOR) out vec4 v_color;

layout(location = V_MATERIAL) out flat uint v_material;

void main() {
  MeshInstance mesh_instance = pc.ub.mesh_instances[gl_BaseInstance].mesh_instance;
  v_material = mesh_instance.material;

  mat4x3 transform_matrix = pc.ub.transform_matrices[gl_BaseInstance].matrix;
  mat3 normal_matrix = pc.ub.normal_matrices[gl_BaseInstance].matrix;

  vec3 position = decode_position(pc.positions[gl_VertexIndex].position);

  position = transform_matrix * vec4(position, 1.0f);
  v_position = position;
  gl_Position = pc.ub.pv * vec4(position, 1.0f);

  vec3 normal = decode_normal(pc.normals[gl_VertexIndex].normal);
  normal = normalize(normal_matrix * normal);
  v_normal = normal;

  if (OPAQUE_FEATURE_TS) {
    vec4 tangent = pc.tangents[gl_VertexIndex].tangent;
    tangent.xyz = normalize((transform_matrix * vec4(tangent.xyz, 0.0f)).xyz);
    vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
    v_tangent = tangent.xyz;
    v_bitangent = bitangent;
  }

  if (OPAQUE_FEATURE_UV) {
    v_uv = pc.uvs[gl_VertexIndex].uv;
  }

  if (OPAQUE_FEATURE_VC) {
    v_color = pc.colors[gl_VertexIndex].color;
  }
}
