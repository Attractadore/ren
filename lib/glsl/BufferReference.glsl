#ifndef REN_GLSL_BUFFER_REFERENCE_GLSL
#define REN_GLSL_BUFFER_REFERENCE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

#define GLSL_REF_TYPE(alignment)                                                 \
  layout(buffer_reference, scalar, buffer_reference_align = alignment) buffer

#define GLSL_REF(RefType) RefType 

#define GLSL_IS_NULL(ref) (uint64_t(ref) == 0) 

#define GLSL_SIZEOF(RefType) (uint64_t(RefType(uint64_t(0))+1))

#endif // REN_GLSL_BUFFER_REFERENCE_GLSL
