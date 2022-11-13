cmake_minimum_required(VERSION 3.23)
project(Renderer2)

set(REN_INCLUDE ${PROJECT_SOURCE_DIR}/include)
set(REN_INCLUDE_GE $<BUILD_INTERFACE:${REN_INCLUDE}>
                   $<INSTALL_INTERFACE:include>)
set(EXTERNAL_DIR ${PROJECT_SOURCE_DIR}/external)

add_subdirectory(lib)

set(BUILD_EXAMPLES
    FALSE
    CACHE BOOL "Build example excutables")
if(${BUILD_EXAMPLES})
  add_subdirectory(examples)
endif()

add_library(ren-hpp INTERFACE)
target_link_libraries(ren-hpp INTERFACE ren)

add_library(ren-hpp-vk INTERFACE)
target_link_libraries(ren-hpp-vk INTERFACE ren-vk)

target_sources(ren PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                          ${REN_INCLUDE}/ren/ren.h)
target_sources(ren-hpp PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                             ${REN_INCLUDE}/ren/ren.hpp)
target_sources(ren-vk PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                 FILES ${REN_INCLUDE}/ren/ren-vk.h)
target_sources(ren-hpp-vk PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                    FILES ${REN_INCLUDE}/ren/ren-vk.hpp)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(
  TARGETS ren ren-hpp
  EXPORT RenTargets
  FILE_SET HEADERS)
install(
  EXPORT RenTargets
  NAMESPACE ren::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
install(
  TARGETS ren-vk ren-hpp-vk
  EXPORT RenVkTargets
  FILE_SET HEADERS)
install(
  EXPORT RenVkTargets
  NAMESPACE ren::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  VERSION 0.1.0
  COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
