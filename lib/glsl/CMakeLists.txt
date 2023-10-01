include(GLSLCShader)

option(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO
       "Compile shaders with source-level debug info" OFF)
set(GLSL_FLAGS -Os)
if(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO)
  message(STATUS "Enable source-level debug info for shaders")
  set(GLSL_FLAGS "${GLSL_FLAGS} -g")
endif()

macro(add_embedded_shader source target)
  add_glslc_shader(${source} GLSLC_FLAGS ${GLSL_FLAGS} ${ARGV} EMBED_TARGET
                   ${target})
  target_link_libraries(ren PRIVATE ${target})
endmacro()

add_embedded_shader(EarlyZ.vert EarlyZPassVertexShader)
add_embedded_shader(Opaque.vert OpaquePassVertexShader)
add_embedded_shader(Opaque.frag OpaquePassFragmentShader)
add_embedded_shader(PostProcessing.comp PostProcessingShader)
add_embedded_shader(ReduceLuminanceHistogram.comp
                    ReduceLuminanceHistogramShader)
add_embedded_shader(ImGui.vert ImGuiVertexShader)
add_embedded_shader(ImGui.frag ImGuiFragmentShader)
