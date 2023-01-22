include(DXCShader)

set(ALBEDO_PERMUTATIONS ALBEDO_CONST ALBEDO_VERTEX)

set(shaders "")

macro(add_shader source output)
  add_dxc_shader(
    ${source}
    ${ARGV}
    OUTPUT_FILE
    ${REN_ASSETS_OUTPUT_DIRECTORY}/${output}
    DXC_FLAGS
    -Zpc
    -fvk-use-dx-position-w
    -fvk-use-scalar-layout
    -spirv
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR}/../include/hlsl)
  list(APPEND shaders ${REN_ASSETS_OUTPUT_DIRECTORY}/${output})
endmacro()

foreach(albedo_perm ${ALBEDO_PERMUTATIONS})
  add_shader(VertexShader.hlsl VertexShader_${albedo_perm}.spv PROFILE vs_6_0
             DEFINES ${albedo_perm})
  add_shader(FragmentShader.hlsl FragmentShader_${albedo_perm}.spv PROFILE
             ps_6_0 DEFINES ${albedo_perm})
endforeach()

add_shader(
  VertexShader.hlsl
  ReflectionVertexShader.spv
  PROFILE
  vs_6_0
  DXC_FLAGS
  -O0
  DEFINES
  REFLECTION)
add_shader(
  FragmentShader.hlsl
  ReflectionFragmentShader.spv
  PROFILE
  ps_6_0
  DXC_FLAGS
  -O0
  DEFINES
  REFLECTION)

add_custom_target(Shaders ALL DEPENDS ${shaders})
