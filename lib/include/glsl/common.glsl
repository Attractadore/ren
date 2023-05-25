#ifndef REN_GLSL_COMMON_GLSL
#define REN_GLSL_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_image_load_formatted : require

#define CAT_IMPL(A, B) A##B
#define CAT(A, B) CAT_IMPL(A, B)

#define UBO(S, B)                                                              \
  layout(set = S, binding = B, scalar) uniform CAT(UBO, __LINE__)

#define SSBO(S, B, qual)                                                       \
  layout(set = S, binding = B, scalar) qual buffer CAT(SSBO, __LINE__)

#define PUSH_CONSTANTS layout(push_constant, scalar) uniform PC

#define SAMPLER_STATE(S, B) layout(set = S, binding = B) uniform sampler

#define SAMPLER2D(S, B) layout(set = S, binding = B) uniform sampler2D

#define TEXTURE2D(S, B) layout(set = S, binding = B) uniform texture2D

#define IMAGE2D(S, B, qual) layout(set = S, binding = B) qual uniform image2D

#define REN_BUFFER_REFERENCE(alignment)                                        \
  layout(buffer_reference, scalar, buffer_reference_align = alignment) buffer

#define REN_REFERENCE(type) type

#define IS_NULL(ref) (uint64_t(ref) == 0)

#define IN(L) layout(location = L) in
#define OUT(L) layout(location = L) out

#define NUM_THREADS(x, y, z)                                                   \
  layout(local_size_x = x, local_size_y = y, local_size_z = z) in

#define assert(expr)
#define static_assert(expr)

#define inline

#define REN_NAMESPACE_BEGIN
#define REN_NAMESPACE_END

#endif // REN_GLSL_COMMON_GLSL
