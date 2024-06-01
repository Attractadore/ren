#ifndef REN_GLSL_UTIL_GLSL
#define REN_GLSL_UTIL_GLSL

#extension GL_EXT_scalar_block_layout : require

#define PUSH_CONSTANTS(PcType)                                                 \
  layout(push_constant, scalar) uniform PC { PcType pc; }

#define NUM_THREADS_3D(x, y, z) layout(local_size_x = x, local_size_y = y, local_size_z = z) in
#define NUM_THREADS_2D(x, y) NUM_THREADS_3D(x, y, 1) 
#define NUM_THREADS(x) NUM_THREADS_3D(x, 1, 1) 

#define SPEC_CONSTANT(id) layout(constant_id = id) const

#endif // REN_GLSL_UTIL_GLSL
