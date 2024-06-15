include(GLSLCShader)

option(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO
       "Compile shaders with source-level debug info" OFF)
set(GLSL_FLAGS -Os)
if(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO)
  message(STATUS "Enable source-level debug info for shaders")
  list(APPEND GLSLC_FLAGS -g)
endif()

macro(add_embedded_shader source target)
  add_glslc_shader(${source} GLSLC_FLAGS ${GLSL_FLAGS} EMBED_TARGET
                   ${target})
  target_link_libraries(ren PRIVATE ${target})
endmacro()

add_embedded_shader(EarlyZ.vert EarlyZVS)

add_embedded_shader(InstanceCullingAndLOD.comp InstanceCullingAndLODCS)
add_embedded_shader(Opaque.vert OpaqueVS)
add_embedded_shader(Opaque.frag OpaqueFS)

add_embedded_shader(PostProcessing.comp PostProcessingCS)
add_embedded_shader(ReduceLuminanceHistogram.comp ReduceLuminanceHistogramCS)

add_embedded_shader(ImGui.vert ImGuiVS)
add_embedded_shader(ImGui.frag ImGuiFS)
