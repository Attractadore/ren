cmake_minimum_required(VERSION 3.23)
project(Renderer2)

set(REN_INCLUDE ${PROJECT_SOURCE_DIR}/include)
set(REN_INCLUDE_GE $<BUILD_INTERFACE:${REN_INCLUDE}>
                   $<INSTALL_INTERFACE:include>)

add_subdirectory(lib)
add_subdirectory(examples)

add_library(RenHpp INTERFACE)
target_include_directories(RenHpp INTERFACE ${REN_INCLUDE_GE})
target_link_libraries(RenHpp INTERFACE Ren)

add_library(RenHpp-Vulkan INTERFACE)
target_include_directories(RenHpp-Vulkan INTERFACE ${REN_INCLUDE_GE})
target_link_libraries(RenHpp-Vulkan INTERFACE Ren-Vulkan)

target_sources(Ren PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                          ${REN_INCLUDE}/Ren/Ren.h)
target_sources(RenHpp PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                             ${REN_INCLUDE}/RenHpp/Ren.hpp)
target_sources(Ren-Vulkan PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                 FILES ${REN_INCLUDE}/Ren/RenVulkan.h)
target_sources(RenHpp-Vulkan PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                    FILES ${REN_INCLUDE}/RenHpp/RenVulkan.hpp)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(
  TARGETS Ren RenHpp
  EXPORT RenTargets
  FILE_SET HEADERS)
install(
  EXPORT RenTargets
  NAMESPACE Ren::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
install(
  TARGETS Ren-Vulkan RenHpp-Vulkan
  EXPORT RenVulkanTargets
  FILE_SET HEADERS)
install(
  EXPORT RenVulkanTargets
  NAMESPACE Ren::
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
