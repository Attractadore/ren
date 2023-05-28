#ifndef REN_GLSL_COMMON_GLSL
#define REN_GLSL_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_debug_printf : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_image_load_formatted : require

#define REN_BUFFER_REFERENCE(alignment)                                        \
  layout(buffer_reference, scalar, buffer_reference_align = alignment) buffer

#define REN_REFERENCE(type) type

#define PUSH_CONSTANTS layout(push_constant, scalar) uniform PC

#define IS_NULL(ref) (uint64_t(ref) == 0)

#define IN(L) layout(location = L) in
#define OUT(L) layout(location = L) out

#define NUM_THREADS(x, y, z)                                                   \
  layout(local_size_x = x, local_size_y = y, local_size_z = z) in

#define ASSERT(expr, what)                                                     \
  if (!(expr)) {                                                               \
    debugPrintfEXT(__FILE__);                                                  \
    debugPrintfEXT(":%d: ");                                                   \
    debugPrintfEXT(what);                                                      \
    debugPrintfEXT(": Assertion failed.\n", __LINE__);                           \
  }
#define assert(expr)                                                           \
  if (!(expr)) {                                                               \
    debugPrintfEXT(__FILE__);                                                  \
    debugPrintfEXT(":%d: Assertion failed.\n", __LINE__);                      \
  }
#define static_assert(expr)

#define inline

#define REN_NAMESPACE_BEGIN
#define REN_NAMESPACE_END

#endif // REN_GLSL_COMMON_GLSL
