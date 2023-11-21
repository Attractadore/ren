#ifndef REN_GLSL_COMMON_GLSL
#define REN_GLSL_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_debug_printf : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_arithmetic: require

#define GLSL_NAMESPACE_BEGIN
#define GLSL_NAMESPACE_END

#define GLSL_BUFFER(alignment)                                                 \
  layout(buffer_reference, scalar, buffer_reference_align = alignment) buffer

#define GLSL_BUFFER_REFERENCE(type) type

#define GLSL_RESTRICT restrict
#define GLSL_READONLY readonly
#define GLSL_WRITEONLY writeonly

#define assert(expr)
#define static_assert(expr)

#define inline

#define alignas(alignment)

#endif // REN_GLSL_COMMON_GLSL
