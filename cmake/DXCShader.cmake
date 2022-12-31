cmake_minimum_required(VERSION 3.20)

function(add_dxc_shader SHADER_TARGET SHADER_SOURCE)
  set(ARGS_OPTIONS EMBEDDED SPIRV)
  set(ARGS_ONE PROFILE)
  set(ARGS_MULTI DXC_FLAGS INCLUDE_DIRECTORIES DEFINES)
  cmake_parse_arguments(PARSE_ARGV 2 OPTION "${ARGS_OPTIONS}" "${ARGS_ONE}"
                        "${ARGS_MULTI}")

  find_package(
    Vulkan
    COMPONENTS dxc
    QUIET)
  if(NOT DXC)
    if(TARGET Vulkan::dxc_exe)
      message(STATUS "Using DXC from Vulkan SDK")
      set(DXC
          $<TARGET_FILE:Vulkan::dxc_exe>
          CACHE FILEPATH "Path to DirectXShaderCompiler")
    else()
      message(STATUS "Using system DXC")
      find_program(
        DXC
        NAMES dxc REQUIRED
        DOC "Path to DirectXShaderCompiler")
    endif()
  endif()

  cmake_path(GET SHADER_SOURCE PARENT_PATH SHADER_REL_DIR)
  cmake_path(RELATIVE_PATH SHADER_REL_DIR)
  cmake_path(GET SHADER_SOURCE FILENAME SHADER_SOURCE)
  cmake_path(SET SHADER_SOURCE_DIR
             ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_REL_DIR})
  cmake_path(SET SHADER_SOURCE_FILE ${SHADER_SOURCE_DIR}/${SHADER_SOURCE})
  if(NOT EXISTS ${SHADER_SOURCE_FILE})
    message(
      FATAL_ERROR "Could not find shader source file ${SHADER_SOURCE_FILE}")
  endif()

  set(DXC_INCLUDE_FLAGS "")
  foreach(inc_dir ${OPTION_INCLUDE_DIRECTORIES})
    list(APPEND DXC_INCLUDE_FLAGS
         -I$<PATH:ABSOLUTE_PATH,${inc_dir},${CMAKE_CURRENT_SOURCE_DIR}>)
  endforeach()

  set(DXC_DEFINE_FLAGS "")
  foreach(def ${OPTION_DEFINES})
    list(APPEND DXC_DEFINE_FLAGS -D${def})
  endforeach()

  set(DXC_FLAGS
      ${OPTION_DXC_FLAGS}
      -T
      ${OPTION_PROFILE}
      -HV
      2021
      ${DXC_INCLUDE_FLAGS}
      ${DXC_DEFINE_FLAGS})
  if(OPTION_SPIRV)
    set(DXC_FLAGS ${DXC_FLAGS} -spirv)
  endif()

  if(OPTION_EMBEDDED)
    set(SHADER_INC ${SHADER_TARGET}.inc)
    cmake_path(SET SHADER_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_INC})

    add_custom_command(
      OUTPUT ${SHADER_INC_FILE}
      DEPENDS ${SHADER_SOURCE_FILE}
      COMMAND
        ${DXC} ${DXC_FLAGS} ${SHADER_SOURCE_FILE} -Fh ${SHADER_INC_FILE} -Vn
        ${SHADER_TARGET} && ${DXC} ${DXC_FLAGS} ${SHADER_SOURCE_FILE} -Fo
        ${SHADER_INC_FILE} -MD -MF ${SHADER_INC_FILE}.d
      COMMAND_EXPAND_LISTS
      DEPFILE ${SHADER_INC_FILE}.d)
    set(SHADER_INC_TARGET ${SHADER_TARGET}-inc)
    add_custom_target(${SHADER_INC_TARGET} DEPENDS ${SHADER_INC_FILE})

    set(SHADER_H ${SHADER_TARGET}.h)
    cmake_path(SET SHADER_H_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_H})

    set(SHADER_H_CODE
        "#pragma once

#ifdef __cplusplus
extern \"C\" {
#endif

static
#include \"${SHADER_INC}\"

#ifdef __cplusplus
}
#endif
")
    file(WRITE ${SHADER_H_FILE} ${SHADER_H_CODE})

    add_library(${SHADER_TARGET} INTERFACE ${SHADER_H_FILE})
    target_include_directories(${SHADER_TARGET}
                               INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

    add_dependencies(${SHADER_TARGET} ${SHADER_INC_TARGET})
  else()
    set(blob_suffix dxil)
    if(OPTION_SPIRV)
      set(blob_suffix spv)
    endif()
    set(SHADER_BLOB ${SHADER_TARGET}.${blob_suffix})
    cmake_path(SET SHADER_BLOB_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_BLOB})

    add_custom_command(
      OUTPUT ${SHADER_BLOB_FILE}
      DEPENDS ${SHADER_SOURCE_FILE}
      COMMAND
        ${DXC} ${DXC_FLAGS} ${SHADER_SOURCE_FILE} -Fo ${SHADER_BLOB_FILE} &&
        ${DXC} ${DXC_FLAGS} ${SHADER_SOURCE_FILE} -Fo ${SHADER_BLOB_FILE} -MD
        -MF ${SHADER_BLOB_FILE}.d
      COMMAND_EXPAND_LISTS
      DEPFILE ${SHADER_BLOB_FILE}.d)
    add_custom_target(${SHADER_TARGET} ALL DEPENDS ${SHADER_BLOB_FILE})
  endif()

endfunction()

function(add_vulkan_shader target source)
  add_dxc_shader(
    ${target}
    ${source}
    ${ARGV}
    SPIRV
    DXC_FLAGS
    -Zpc
    -fvk-use-scalar-layout)
endfunction()
