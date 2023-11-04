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

add_embedded_shader(EarlyZPass.vert EarlyZPassVS)

add_embedded_shader(OpaquePass.vert OpaquePassVS)
add_embedded_shader(OpaquePass.frag OpaquePassFS)

add_embedded_shader(PostProcessing.comp PostProcessingCS)
add_embedded_shader(ReduceLuminanceHistogram.comp ReduceLuminanceHistogramCS)

add_embedded_shader(ImGui.vert ImGuiVS)
add_embedded_shader(ImGui.frag ImGuiFS)
