set(SLANG_FLAGS
  -D__SLANG__=1
  -entry ren::sh::main
  -matrix-layout-row-major
  -profile spirv_1_5
  -target spirv
  -unscoped-enum
  -default-image-format-unknown
  -disable-dynamic-dispatch
  -fvk-use-scalar-layout
  -fspv-reflect
  -Wno-15205
  -Wno-39001
  -Wno-41012
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  list(APPEND SLANG_FLAGS -msvc-style-bitfield-packing)
endif()
if(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO)
  message(STATUS "Enable source-level debug info for shaders")
  list(APPEND SLANG_FLAGS -g3)
endif()

function(add_shader target shader_slang)
  cmake_path(ABSOLUTE_PATH shader_slang)
  if(NOT EXISTS ${shader_slang})
    message(
      FATAL_ERROR "Could not find shader source file ${shader_slang}")
  endif()

  cmake_path(GET shader_slang FILENAME shader_filename)
  cmake_path(GET shader_slang EXTENSION LAST_ONLY shader_ext)
  cmake_path(GET shader_slang STEM shader_name)
  cmake_path(SET shader_dep ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.d)
  cmake_path(SET shader_spv ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.spv)
  cmake_path(SET shader_hpp ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.hpp)
  cmake_path(SET shader_cpp ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.cpp)

  if (shader_ext STREQUAL ".vert")
    set(stage vertex)
  elseif(shader_ext STREQUAL ".frag")
    set(stage fragment)
  elseif(shader_ext STREQUAL ".comp")
    set(stage compute)
  endif()

  add_custom_command(
    OUTPUT ${shader_spv}
    DEPENDS ${shader_slang} "${slangc}"
    COMMAND "${slangc}" ${SLANG_FLAGS} -stage ${stage} -o ${shader_spv} -depfile ${shader_dep} ${shader_slang}
    DEPFILE ${shader_dep}
    COMMAND_EXPAND_LISTS VERBATIM)

  add_custom_command(
    OUTPUT ${shader_hpp} ${shader_cpp}
    DEPENDS ${shader_spv} ren::process-shader
    COMMAND ren::process-shader --project-src-dir "${PROJECT_SOURCE_DIR}" --src "${shader_slang}" "${shader_spv}"
    COMMAND_EXPAND_LISTS VERBATIM)

  set(shader_target ${shader_filename})
  string(REPLACE "." "_" shader_target ${shader_target})
  add_custom_target(${shader_target} DEPENDS ${shader_hpp} ${shader_cpp})

  target_sources(${target} PRIVATE ${shader_hpp} ${shader_cpp})
  target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  add_dependencies(${target} ${shader_target})
endfunction()

add_shader(ren InstanceCullingAndLOD.comp)
add_shader(ren MeshletCulling.comp)
add_shader(ren MeshletSorting.comp)
add_shader(ren ExclusiveScanUint32.comp)
add_shader(ren PrepareBatch.comp)

add_shader(ren EarlyZ.vert)
add_shader(ren Opaque.vert)
add_shader(ren Opaque.frag)
add_shader(ren Skybox.vert)
add_shader(ren Skybox.frag)
add_shader(ren ImGui.vert)
add_shader(ren ImGui.frag)

add_shader(ren HiZ.comp)
add_shader(ren SsaoHiZ.comp)
add_shader(ren Ssao.comp)
add_shader(ren SsaoFilter.comp)
add_shader(ren PostProcessing.comp)
add_shader(ren ReduceLuminanceHistogram.comp)

if (TARGET ren-baking-internal)
  add_shader(ren-baking-internal BakeReflectionMap.comp)
  add_shader(ren-baking-internal BakeSpecularMap.comp)
  add_shader(ren-baking-internal BakeIrradianceMap.comp)
endif()
