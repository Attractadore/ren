include(DXCShader)
include(ShaderVars)

set(directx12_shaders "")

macro(add_directx12_shader source output)
  add_dxc_shader(
    ${REN_SHADER_DIR}/${source}
    ${ARGV}
    OUTPUT_FILE
    ${REN_ASSETS_OUTPUT_DIRECTORY}/${output}
    DXC_FLAGS
    -Zpc
    -no-legacy-cbuf-layout
    -Qstrip_reflect
    INCLUDE_DIRECTORIES
    ${REN_PRIVATE_INCLUDE_DIR}/hlsl)
  list(APPEND directx12_shaders ${REN_ASSETS_OUTPUT_DIRECTORY}/${output})
endmacro()

macro(embed_directx12_shader source target)
  add_dxc_shader(
    ${source}
    ${ARGV}
    EMBED_TARGET
    ${target}
    DXC_FLAGS
    -Zpc
    -no-legacy-cbuf-layout
    INCLUDE_DIRECTORIES
    ${REN_PRIVATE_INCLUDE_DIR}/hlsl)
endmacro()

foreach(vf_perm VERTEX_FETCH_LOGICAL VERTEX_FETCH_ATTRIBUTE)
  foreach(albedo_perm ${ALBEDO_PERMUTATIONS})
    add_directx12_shader(
      VertexShader.hlsl
      VertexShader_${vf_perm}_${albedo_perm}.dxil
      PROFILE
      vs_6_0
      DEFINES
      ${vf_perm}
      ${albedo_perm})
    add_directx12_shader(
      FragmentShader.hlsl
      FragmentShader_${vf_perm}_${albedo_perm}.dxil
      PROFILE
      ps_6_0
      DEFINES
      ${vf_perm}
      ${albedo_perm})
  endforeach()
endforeach()

add_directx12_shader(
  VertexShader.hlsl
  ReflectionVertexShader.dxil
  REFLECTION_FILE
  ${REN_ASSETS_OUTPUT_DIRECTORY}/ReflectionVertexShader.refl
  PROFILE
  vs_6_0
  DEFINES
  REFLECTION
)
add_directx12_shader(
  FragmentShader.hlsl
  ReflectionFragmentShader.dxil
  REFLECTION_FILE
  ${REN_ASSETS_OUTPUT_DIRECTORY}/ReflectionFragmentShader.refl
  PROFILE
  ps_6_0
  DEFINES
  REFLECTION
)

embed_directx12_shader(FullScreenRect.hlsl FullScreenRectShader PROFILE vs_6_0)
embed_directx12_shader(BlitToSwapchain.hlsl BlitToSwapchainShader PROFILE
                       ps_6_0)

add_custom_target(DirectX12Shaders ALL DEPENDS ${directx12_shaders})
