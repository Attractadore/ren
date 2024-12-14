#ifndef REN_GLSL_COMMON_H
#define REN_GLSL_COMMON_H

#if GL_core_profile

#pragma use_vulkan_memory_model
#extension GL_EXT_debug_printf : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_image_load_formatted : require
#extension GL_KHR_memory_scope_semantics : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require

#define static_assert(expr)
#define inline
#define alignas(alignment)

#define GLSL_NAMESPACE_BEGIN
#define GLSL_NAMESPACE_END

#define GLSL_RESTRICT restrict
#define GLSL_READONLY readonly
#define GLSL_WRITEONLY writeonly

#define GLSL_IN(T) T
#define GLSL_OUT(T) out T
#define GLSL_INOUT(T) inout T

#define GLSL_PUSH_CONSTANTS layout(push_constant, scalar) uniform
#define GLSL_PC pc

#define SPEC_CONSTANT(id) layout(constant_id = id) const

#define LOCAL_SIZE_3D(x, y, z)                                                 \
  layout(local_size_x = x, local_size_y = y, local_size_z = z) in
#define LOCAL_SIZE_2D(x, y) LOCAL_SIZE_3D(x, y, 1)
#define LOCAL_SIZE(x) LOCAL_SIZE_3D(x, 1, 1)

#else

#include <concepts>
#include <cstdint>
#include <glm/glm.hpp>

#define GLSL_NAMESPACE_BEGIN namespace ren::glsl {
#define GLSL_NAMESPACE_END }

#define GLSL_RESTRICT
#define GLSL_READONLY
#define GLSL_WRITEONLY

#define GLSL_IN(T) const T &
#define GLSL_OUT(T) T &
#define GLSL_INOUT(T) T &

#define GLSL_PUSH_CONSTANTS struct
#define GLSL_PC

GLSL_NAMESPACE_BEGIN
using namespace glm;
GLSL_NAMESPACE_END

#endif

#endif // REN_GLSL_COMMON_H
