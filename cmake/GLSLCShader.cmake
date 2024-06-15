cmake_minimum_required(VERSION 3.20)

find_program(REN_GLSLC glslc DOC "Path to glslc")
find_package(Vulkan COMPONENTS glslc)
if (Vulkan::glslc)
  message(STATUS "Using glslc from Vulkan SDK")
  set(REN_GLSLC $<TARGET_FILE::Vulkan::glslc> CACHE PATH "" FORCE)
endif()
if (NOT REN_GLSLC)
  message(FATAL_ERROR "Failed to find glslc")
endif()

function(add_glslc_shader SHADER_SOURCE)
  set(ARGS_OPTIONS STUB)
  set(ARGS_ONE EMBED_TARGET OUTPUT_FILE)
  set(ARGS_MULTI GLSLC_FLAGS INCLUDE_DIRECTORIES DEFINES)
  cmake_parse_arguments(PARSE_ARGV 1 OPTION "${ARGS_OPTIONS}" "${ARGS_ONE}"
                        "${ARGS_MULTI}")

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

  set(GLSLC_INCLUDE_FLAGS "")
  foreach(inc_dir ${OPTION_INCLUDE_DIRECTORIES})
    list(APPEND GLSLC_INCLUDE_FLAGS
         -I$<PATH:ABSOLUTE_PATH,${inc_dir},${CMAKE_CURRENT_SOURCE_DIR}>)
  endforeach()

  set(GLSLC_DEFINE_FLAGS "")
  foreach(def ${OPTION_DEFINES})
    list(APPEND GLSLC_DEFINE_FLAGS -D${def})
  endforeach()

  set(GLSLC_FLAGS ${OPTION_GLSLC_FLAGS}
                  ${GLSLC_INCLUDE_FLAGS} ${GLSLC_DEFINE_FLAGS})

  if(OPTION_EMBED_TARGET)
    set(SHADER_INC ${OPTION_EMBED_TARGET}.inc)
    set(SHADER_H ${OPTION_EMBED_TARGET}.h)
    set(SHADER_C ${OPTION_EMBED_TARGET}.c)
    cmake_path(SET SHADER_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_INC})
    cmake_path(SET SHADER_H_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_H})
    cmake_path(SET SHADER_C_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_C})

    file(
      WRITE ${SHADER_H_FILE}
      "
      #pragma once
      #include <stddef.h>
      #include <stdint.h>

      #ifdef __cplusplus
      extern \"C\" {
      #endif

      extern const uint32_t ${OPTION_EMBED_TARGET}[];
      extern const size_t ${OPTION_EMBED_TARGET}_count;

      #ifdef __cplusplus
      }
      #endif")

    file(
      WRITE ${SHADER_C_FILE}
      "
      #include \"${SHADER_H_FILE}\"

      const uint32_t ${OPTION_EMBED_TARGET}[] = {
        #include \"${SHADER_INC_FILE}\"
      };
      const size_t ${OPTION_EMBED_TARGET}_count = sizeof(${OPTION_EMBED_TARGET}) / sizeof(uint32_t);")

    add_custom_command(
      OUTPUT ${SHADER_INC_FILE}
      DEPENDS ${SHADER_SOURCE_FILE}
      COMMAND ${REN_GLSLC} ${GLSLC_FLAGS} ${SHADER_SOURCE_FILE} -mfmt=num -o
              ${SHADER_INC_FILE} -MD -MF ${SHADER_INC_FILE}.d
      COMMAND_EXPAND_LISTS
      DEPFILE ${SHADER_INC_FILE}.d)
    set(SHADER_INC_TARGET ${OPTION_EMBED_TARGET}-Inc)
    add_custom_target(${SHADER_INC_TARGET} DEPENDS ${SHADER_INC_FILE})

    add_library(${OPTION_EMBED_TARGET} STATIC ${SHADER_H_FILE} ${SHADER_C_FILE})
    target_include_directories(${OPTION_EMBED_TARGET}
                               PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    add_dependencies(${OPTION_EMBED_TARGET} ${SHADER_INC_TARGET})
  else()
    set(SHADER_BINARY_FILE ${OPTION_OUTPUT_FILE})
    add_custom_command(
      OUTPUT ${SHADER_BINARY_FILE}
      DEPENDS ${SHADER_SOURCE_FILE}
      COMMAND ${REN_GLSLC} ${GLSLC_FLAGS} ${SHADER_SOURCE_FILE} -o
              ${SHADER_BINARY_FILE} -MD -MF ${SHADER_BINARY_FILE}.d
      COMMAND_EXPAND_LISTS
      DEPFILE ${SHADER_BINARY_FILE}.d)
  endif()

endfunction()
