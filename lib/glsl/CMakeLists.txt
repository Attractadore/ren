set(SHADER_FLAGS)
option(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO "Compile shaders with source-level debug info" OFF)
if(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO)
  message(STATUS "Enable source-level debug info for shaders")
  list(APPEND SHADER_FLAGS -g)
endif()

function(add_shader shader_glsl)
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
    DEPENDS ${shader_glsl} $<TARGET_FILE:ren::shader-compiler>
    COMMAND $<TARGET_FILE:ren::shader-compiler> ${SHADER_FLAGS} -o ${CMAKE_CURRENT_BINARY_DIR} --depfile ${shader_dep} --project-src-dir "${PROJECT_SOURCE_DIR}" ${shader_glsl}
    DEPFILE ${shader_dep}
    COMMAND_EXPAND_LISTS VERBATIM)
  add_custom_target(${shader_target} DEPENDS ${shader_hpp} ${shader_cpp})

  target_sources(ren PRIVATE ${shader_hpp} ${shader_cpp})
  target_include_directories(ren PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

add_shader(CalculateNormalMatrices.comp)

add_shader(InstanceCullingAndLOD.comp)
add_shader(MeshletCulling.comp)
add_shader(MeshletSorting.comp)
add_shader(ExclusiveScanUint32.comp)
add_shader(PrepareBatch.comp)

add_shader(EarlyZ.vert)
add_shader(Opaque.vert)
add_shader(Opaque.frag)
add_shader(ImGui.vert)
add_shader(ImGui.frag)

add_shader(HiZSpd.comp)
add_shader(PostProcessing.comp)
add_shader(ReduceLuminanceHistogram.comp)
