include(GLSLCShader)

set(GLSL_FLAGS -g)

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
endmacro()

add_embedded_shader(VertexShader.vert VertexShader)
add_embedded_shader(FragmentShader.frag FragmentShader)
add_embedded_shader(ReinhardTonemap.comp ReinhardTonemapShader)

add_embedded_shader(VertexShader.vert ReflectionVertexShader GLSLC_FLAGS
                    -fpreserve-bindings)
add_embedded_shader(FragmentShader.frag ReflectionFragmentShader GLSLC_FLAGS
                    -fpreserve-bindings)
