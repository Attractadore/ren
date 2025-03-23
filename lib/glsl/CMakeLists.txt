set(SHADER_FLAGS)
if(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO)
  message(STATUS "Enable source-level debug info for shaders")
  list(APPEND SHADER_FLAGS -g)
endif()

function(add_shader target shader_glsl)
  cmake_path(ABSOLUTE_PATH shader_glsl)
  if(NOT EXISTS ${shader_glsl})
    message(
      FATAL_ERROR "Could not find shader source file ${shader_glsl}")
  endif()

  cmake_path(GET shader_glsl FILENAME shader_filename)
  cmake_path(GET shader_glsl STEM shader_name)
  cmake_path(SET shader_dep ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.d)
  cmake_path(SET shader_spv ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.spv)
  cmake_path(SET shader_hpp ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.hpp)
  cmake_path(SET shader_cpp ${CMAKE_CURRENT_BINARY_DIR}/${shader_filename}.cpp)

  set(shader_target ${shader_filename})
  string(REPLACE "." "_" shader_target ${shader_target})

  add_custom_command(
    OUTPUT ${shader_spv} ${shader_hpp} ${shader_cpp}
    DEPENDS ${shader_glsl} ren::shader-compiler
    COMMAND ren::shader-compiler ${SHADER_FLAGS} -o ${CMAKE_CURRENT_BINARY_DIR} --depfile ${shader_dep} --project-src-dir "${PROJECT_SOURCE_DIR}" ${shader_glsl}
    DEPFILE ${shader_dep}
    COMMAND_EXPAND_LISTS VERBATIM)
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

add_shader(ren HiZSpd.comp)
add_shader(ren PostProcessing.comp)
add_shader(ren ReduceLuminanceHistogram.comp)

if (TARGET ren-baking)
  add_shader(ren-baking BakeDhrLut.comp)
  add_shader(ren-baking BakeReflectionMap.comp)
  add_shader(ren-baking BakeSpecularMap.comp)
  add_shader(ren-baking BakeIrradianceMap.comp)
endif()
