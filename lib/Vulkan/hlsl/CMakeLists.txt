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
    # -fspv-flatten-resource-arrays
    -fvk-use-dx-position-w
    -fvk-use-scalar-layout
    -spirv
    INCLUDE_DIRECTORIES
    ${REN_PRIVATE_INCLUDE_DIR}/hlsl)
  list(APPEND vulkan_shaders ${REN_ASSETS_OUTPUT_DIRECTORY}/${output})
endmacro()

foreach(vf_perm VERTEX_FETCH_PHYSICAL VERTEX_FETCH_ATTRIBUTE)
  foreach(albedo_perm ${ALBEDO_PERMUTATIONS})
    add_vulkan_shader(
      VertexShader.hlsl
      VertexShader_${vf_perm}_${albedo_perm}.spv
      PROFILE
      vs_6_0
      DEFINES
      ${vf_perm}
      ${albedo_perm})
    add_vulkan_shader(
      FragmentShader.hlsl
      FragmentShader_${vf_perm}_${albedo_perm}.spv
      PROFILE
      ps_6_0
      DEFINES
      ${vf_perm}
      ${albedo_perm})
  endforeach()
endforeach()

add_vulkan_shader(
  VertexShader.hlsl
  ReflectionVertexShader.spv
  PROFILE
  vs_6_0
  DXC_FLAGS
  -fspv-reflect
  -O0
  DEFINES
  REFLECTION)
add_vulkan_shader(
  FragmentShader.hlsl
  ReflectionFragmentShader.spv
  PROFILE
  ps_6_0
  DXC_FLAGS
  -fspv-reflect
  -O0
  DEFINES
  REFLECTION)

add_custom_target(VulkanShaders ALL DEPENDS ${vulkan_shaders})
