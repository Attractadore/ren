cmake_minimum_required(VERSION 3.20)

function(add_dxc_shader SHADER_TARGET SHADER_SOURCE)
  set(ARGS_OPTIONS EMBEDDED)
  set(ARGS_ONE PROFILE)
  set(ARGS_MULTI _MULTI_STUB)
  cmake_parse_arguments(PARSE_ARGV 2 OPTION ${ARGS_OPTIONS} ${ARGS_ONE}
                        ${ARGS_MULTI})

  find_package(Vulkan COMPONENTS dxc)
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

  if(OPTION_EMBEDDED)
    cmake_path(SET SHADER_BINARY_DIR
               ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_REL_DIR})

    cmake_path(REPLACE_EXTENSION SHADER_SOURCE LAST_ONLY inc OUTPUT_VARIABLE
               SHADER_INC)
    cmake_path(SET SHADER_INC_FILE ${SHADER_BINARY_DIR}/${SHADER_INC})

    set(DXC_FLAGS -T ${OPTION_PROFILE} -HV 2021)

    add_custom_command(
      OUTPUT ${SHADER_INC_FILE}
      DEPENDS ${SHADER_SOURCE_FILE}
      COMMAND
        ${DXC} ${DXC_FLAGS} ${SHADER_SOURCE_FILE} -Fh ${SHADER_INC_FILE} &&
        ${DXC} ${DXC_FLAGS} ${SHADER_SOURCE_FILE} -Fo ${SHADER_INC_FILE} -MD -MF
        ${SHADER_INC_FILE}.d
      COMMAND_EXPAND_LISTS
      DEPFILE ${SHADER_INC_FILE}.d)
    set(SHADER_INC_TARGET ${SHADER_TARGET}-inc)
    add_custom_target(${SHADER_INC_TARGET} DEPENDS ${SHADER_INC_FILE})

    cmake_path(REPLACE_EXTENSION SHADER_SOURCE LAST_ONLY h OUTPUT_VARIABLE
               SHADER_H)
    cmake_path(SET SHADER_H_FILE ${SHADER_BINARY_DIR}/${SHADER_H})

    set(SHADER_H_CODE
        "
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern \"C\" {
#endif

#define g_main ${SHADER_TARGET}
static
#include \"${SHADER_INC}\"
#undef g_main

#ifdef __cplusplus
}
#endif
")
    file(WRITE ${SHADER_H_FILE} ${SHADER_H_CODE})

    add_library(${SHADER_TARGET} INTERFACE ${SHADER_H_FILE})
    target_include_directories(${SHADER_TARGET} INTERFACE ${SHADER_BINARY_DIR})

    add_dependencies(${SHADER_TARGET} ${SHADER_INC_TARGET})
  else()
    message(FATAL_ERROR "Only embedded shaders are currently supported")
  endif()

endfunction()
