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

foreach(perm ${ALBEDO_PERMUTATIONS})
  add_directx12_shader(VertexShader.hlsl VertexShader_${perm}.dxil PROFILE
                       vs_6_0 DEFINES ${perm})
  add_directx12_shader(FragmentShader.hlsl FragmentShader_${perm}.dxil PROFILE
                       ps_6_0 DEFINES ${perm})
endforeach()

add_directx12_shader(
  VertexShader.hlsl ReflectionVertexShader.dxil PROFILE vs_6_0 REFLECTION_FILE
  ${REN_ASSETS_OUTPUT_DIRECTORY}/ReflectionVertexShader.refl)
add_directx12_shader(
  FragmentShader.hlsl ReflectionFragmentShader.dxil PROFILE ps_6_0
  REFLECTION_FILE ${REN_ASSETS_OUTPUT_DIRECTORY}/ReflectionFragmentShader.refl)

embed_directx12_shader(FullScreenRect.hlsl FullScreenRectShader PROFILE vs_6_0)
embed_directx12_shader(BlitToSwapchain.hlsl BlitToSwapchainShader PROFILE
                       ps_6_0)

add_custom_target(DirectX12Shaders ALL DEPENDS ${directx12_shaders})
