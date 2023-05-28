include(GLSLCShader)

set(GLSL_FLAGS -g -fpreserve-bindings -Os)

add_library(EmbeddedShaders INTERFACE)

macro(add_embedded_shader source target)
  add_glslc_shader(
    ${source}
    GLSLC_FLAGS
    ${GLSL_FLAGS}
    ${ARGV}
    EMBED_TARGET
    ${target}
    INCLUDE_DIRECTORIES
    ../include/glsl)
  target_link_libraries(EmbeddedShaders INTERFACE ${target})
endmacro()

add_embedded_shader(VertexShader.vert VertexShader)
add_embedded_shader(FragmentShader.frag FragmentShader)
add_embedded_shader(BuildLuminanceHistogram.comp BuildLuminanceHistogramShader)
add_embedded_shader(ReduceLuminanceHistogram.comp ReduceLuminanceHistogramShader)
add_embedded_shader(ReinhardToneMapping.comp ReinhardToneMappingShader)
