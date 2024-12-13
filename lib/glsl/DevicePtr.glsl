#ifndef REN_GLSL_DEVICE_PTR_GLSL
#define REN_GLSL_DEVICE_PTR_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#define GLSL_PTR_IMPL(Type) Type ## _Ptr
#define GLSL_PTR(Type) GLSL_PTR_IMPL(Type)

#define GLSL_DEFINE_PTR_TYPE(Type, alignment) layout(buffer_reference, scalar, buffer_reference_align = alignment) buffer GLSL_PTR(Type) { Type data; }

#define IS_NULL_PTR(ptr) (uint64_t(ptr) == 0)

#define DEREF(ref) ((ref).data)

layout(buffer_reference, scalar, buffer_reference_align = 1) buffer GLSL_PTR(void) { uint8_t no_data; };

#endif // REN_GLSL_DEVICE_PTR_GLSL
