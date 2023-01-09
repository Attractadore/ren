include(DXCShader)
include(ShaderVars)

set(vulkan_shaders "")

macro(add_vulkan_shader source output)
  add_dxc_shader(
    ${REN_SHADER_DIR}/${source}
    ${ARGV}
    OUTPUT_FILE
    ${REN_ASSETS_OUTPUT_DIRECTORY}/${output}
    DXC_FLAGS
    -Zpc
    -fvk-use-scalar-layout
    -fvk-use-dx-position-w
    -spirv
    INCLUDE_DIRECTORIES
    ${REN_PRIVATE_INCLUDE_DIR}/hlsl)
  list(APPEND vulkan_shaders ${REN_ASSETS_OUTPUT_DIRECTORY}/${output})
endmacro()

foreach(perm ${ALBEDO_PERMUTATIONS})
  add_vulkan_shader(VertexShader.hlsl VertexShader_${perm}.spv PROFILE vs_6_0
                    DEFINES ${perm})
  add_vulkan_shader(FragmentShader.hlsl FragmentShader_${perm}.spv PROFILE
                    ps_6_0 DEFINES ${perm})
endforeach()

add_vulkan_shader(
  VertexShader.hlsl
  ReflectionVertexShader.spv
  PROFILE
  vs_6_0
  DXC_FLAGS
  -fspv-reflect
  -O0)
add_vulkan_shader(
  FragmentShader.hlsl
  ReflectionFragmentShader.spv
  PROFILE
  ps_6_0
  DXC_FLAGS
  -fspv-reflect
  -O0)

add_custom_target(VulkanShaders ALL DEPENDS ${vulkan_shaders})
