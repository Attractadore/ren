include(DXCShader)

set(shaders "")

set(SPIRV_FLAGS -Zpc -fspv-debug=vulkan-with-source
                       -fvk-use-dx-position-w -fvk-use-scalar-layout -spirv)

macro(add_shader source output)
  add_dxc_shader(
    ${source}
    ${ARGV}
    OUTPUT_FILE
    ${REN_ASSETS_OUTPUT_DIRECTORY}/${output}
    DXC_FLAGS
    ${SPIRV_FLAGS}
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR}/../include/hlsl)
  list(APPEND shaders ${REN_ASSETS_OUTPUT_DIRECTORY}/${output})
endmacro()

macro(add_embedded_shader source target)
  add_dxc_shader(
    ${source}
    ${ARGV}
    EMBED_TARGET
    ${target}
    DXC_FLAGS
    ${SPIRV_FLAGS}
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR}/../include/hlsl)
endmacro()

add_embedded_shader(VertexShader.hlsl VertexShader PROFILE vs_6_0)
add_embedded_shader(FragmentShader.hlsl FragmentShader PROFILE ps_6_0)
add_embedded_shader(ReinhardTonemap.hlsl ReinhardTonemapShader PROFILE cs_6_0)

add_embedded_shader(
  VertexShader.hlsl
  ReflectionVertexShader
  PROFILE
  vs_6_0
  DXC_FLAGS
  -O0
  DEFINES
  REFLECTION)
add_embedded_shader(
  FragmentShader.hlsl
  ReflectionFragmentShader
  PROFILE
  ps_6_0
  DXC_FLAGS
  -O0
  DEFINES
  REFLECTION)

add_custom_target(Shaders ALL DEPENDS ${shaders})
