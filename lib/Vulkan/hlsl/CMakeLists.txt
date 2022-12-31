include(DXCShader)

function(add_shader target source)
  add_vulkan_shader(
    ${target}
    ${source}
    ${ARGV}
    INCLUDE_DIRECTORIES
    ${REN_PRIVATE_INCLUDE_DIR}/hlsl
    OUTPUT_DIRECTORY
    ${REN_ASSETS_OUTPUT_DIRECTORY})
endfunction()

set(ALBEDO_PERMUTATIONS CONST_COLOR VERTEX_COLOR)
foreach(perm ${ALBEDO_PERMUTATIONS})
  add_shader(VertexShader_${perm} ${REN_SHADER_DIR}/VertexShader.hlsl PROFILE
             vs_6_0 DEFINES ${perm})
  add_shader(FragmentShader_${perm} ${REN_SHADER_DIR}/FragmentShader.hlsl
             PROFILE ps_6_0 DEFINES ${perm})
endforeach()
