cmake_minimum_required(VERSION 3.24)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

option(REN_BUILD_EXAMPLES "Build example excutables" FALSE)
option(REN_BUILD_IMGUI_PLUGIN "Enable ImGui plugin for debug UI rendering" OFF)
option(REN_BUILD_SHADER_COMPILER "Build shader compiler" ON)
option(REN_BUILD_BAKING_TOOLS "Build baking tools" ON)

project(ren VERSION 0.1.0)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wa,-mbig-obj")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj")
  endif()
endif()

if(MSVC)
  set(msvc_flags "/Zc:preprocessor")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Zc:__cplusplus ${msvc_flags}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__STDC__ ${msvc_flags}")
endif()

set(REN_INCLUDE ${PROJECT_SOURCE_DIR}/include)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

add_subdirectory(external)
add_subdirectory(lib)

if(REN_BUILD_EXAMPLES)
  message(STATUS "Build examples")
  add_subdirectory(examples)
endif()
