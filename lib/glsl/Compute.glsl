#ifndef REN_GLSL_COMPUTE_GLSL
#define REN_GLSL_COMPUTE_GLSL

uvec2 linear_to_local_2d(const uvec3 WG_SIZE, uint index) {
  const uint NUM_QUADS_X = WG_SIZE.x / 2;
  uint quad_index = index / 4;
  uvec2 quad_id = uvec2(quad_index % NUM_QUADS_X, quad_index / NUM_QUADS_X);
  uint quad_invocation_index = index % 4;
  uvec2 quad_invocation_id = uvec2(quad_invocation_index % 2, quad_invocation_index / 2);
  return 2 * quad_id + quad_invocation_id;
}

uvec2 linear_to_global_2d(const uvec3 WG_ID, const uvec3 WG_SIZE, uint index) {
  return WG_ID.xy * WG_SIZE.xy + linear_to_local_2d(WG_SIZE, index);
}

#endif // REN_GLSL_COMPUTE_GLSL
