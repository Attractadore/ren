set(REN_LOG_SEVERITY
    "ERROR;WARN;INFO"
    CACHE
      STRING
      "Enable logging for these severity levels (Available levels are: ERROR, WARN, INFO, DEBUG)"
)
option(REN_BUILD_TESTS "Build tests" OFF)

set(REN_PRIVATE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
set(REN_SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/hlsl)

file(
  CONFIGURE
  OUTPUT
  GeneratedConfig.hpp
  CONTENT
  "#pragma once

namespace ren {
constexpr const char* c_assets_dir = \"${REN_ASSETS_OUTPUT_DIRECTORY}\";
}
")
add_library(Generated INTERFACE)
target_include_directories(Generated INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(range-v3 REQUIRED)

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(
  Support
  INTERFACE Generated
            SlotMap
            Boost::boost
            Boost::container
            range-v3::range-v3
            fmt::fmt
            glm::glm)
foreach(SEVERITY ${REN_LOG_SEVERITY})
  message(STATUS "Enable ${SEVERITY} logging")
  target_compile_definitions(Support INTERFACE REN_LOG_${SEVERITY})
endforeach()

if(REN_ERROR_LOG)
  message(STATUS "Log errors")
endif()

add_library(ren ren.cpp RenderGraph.cpp Scene.cpp ResourceUploader.cpp)
target_sources(ren PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                          ${REN_INCLUDE}/ren/ren.h)
target_link_libraries(ren PRIVATE Support)

if(BUILD_SHARED_LIBS)
  add_library(ren::ren ALIAS ren)
else()
  include(BundleDependencies)
  bundle_dependencies(ren ren-bundled)
  set_target_properties(ren-bundled PROPERTIES EXPORT_NAME "ren")
  add_library(ren::ren ALIAS ren-bundled)
endif()

add_library(ren-hpp INTERFACE)
target_sources(ren-hpp PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                              ${REN_INCLUDE}/ren/ren.hpp)
target_link_libraries(ren-hpp INTERFACE ren::ren glm::glm)
target_compile_features(ren-hpp INTERFACE cxx_std_20)
add_library(ren::ren-hpp ALIAS ren-hpp)

message(STATUS "Enable Vulkan renderer")
add_subdirectory(Vulkan)
if(WIN32)
  message(STATUS "Enable DirectX 12 renderer")
  add_subdirectory(DirectX12)
endif()

if(REN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(REN_INSTALL)
  include(GNUInstallDirs)
  get_target_property(ren_target ren::ren ALIASED_TARGET)
  install(
    TARGETS ${ren_target} ren-hpp
    EXPORT ${REN_TARGETS}
    FILE_SET HEADERS)
  if(NOT BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PROPERTY:${ren_target},ARCHIVE_OUTPUT_NAME> TYPE LIB)
  endif()
  install(
    EXPORT ${REN_TARGETS}
    NAMESPACE ren::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
