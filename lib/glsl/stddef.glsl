#ifndef REN_GLSL_SHADER_GLSL
#define REN_GLSL_SHADER_GLSL

#define PUSH_CONSTANTS layout(push_constant, scalar) uniform PC

#define IS_NULL(ref) (uint64_t(ref) == 0)

#define NUM_THREADS(x, y, z)                                                   \
  layout(local_size_x = x, local_size_y = y, local_size_z = z) in

#endif // REN_GLSL_SHADER_GLSL
