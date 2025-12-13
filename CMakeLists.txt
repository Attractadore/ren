cmake_minimum_required(VERSION 3.24)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_SCAN_FOR_MODULES FALSE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

option(REN_ASSERTIONS "Enable assertions" OFF)
option(REN_BUILD_BAKING_TOOLS "Build baking tools" ON)
option(REN_BUILD_EXAMPLES "Build example excutables" ON)
option(REN_BUILD_IMGUI_PLUGIN "Enable ImGui plugin for debug UI rendering" ON)
option(REN_DEBUG_LAYER "Enable RHI debug layer" OFF)
option(REN_HOT_RELOAD "Build with hot reload support" OFF)
option(REN_SHADER_SOURCE_LEVEL_DEBUG_INFO "Compile shaders with source-level debug info" OFF)

project(ren VERSION 0.1.0 LANGUAGES C CXX ASM ASM_MASM)

find_program(ren_ccache ccache)
if(ren_ccache)
  set(CMAKE_C_COMPILER_LAUNCHER ${ren_ccache})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${ren_ccache})
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-Xclang;-fno-pch-timestamp>")
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
find_program(ren_mold mold)
if(ren_mold)
  set(CMAKE_LINKER_TYPE MOLD)
endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wa,-mbig-obj")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj")
  endif()
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:preprocessor /Zc:__cplusplus")
endif()

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-gnu-unique")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-gnu-unique")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  # add_link_options(-fsanitize=address)
  # add_compile_options(-fsanitize=thread)
  # add_link_options(-fsanitize=thread)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Use default warnings for external code
add_subdirectory(external)

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  add_compile_options(
    -fno-strict-aliasing
    -Wall
    -Wextra
    # -Werror
    -Wno-missing-designated-field-initializers
    -Wno-missing-field-initializers
    -Wno-unused-parameter
  )
endif()

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(c_cxx_flags
      -fno-strict-aliasing
      /W4
      # /WX
      -Wno-deprecated-copy
      -Wno-missing-designated-field-initializers
      -Wno-missing-field-initializers
      -Wno-unused-parameter
    )
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${c_cxx_flags}>")
  else()
    # add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Wall>)
  endif()
endif()

set(REN_INCLUDE ${PROJECT_SOURCE_DIR}/include)

add_subdirectory(lib)

if(REN_BUILD_EXAMPLES)
  message(STATUS "Build examples")
  add_subdirectory(examples)
endif()

add_subdirectory(editor)
