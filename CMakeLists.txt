cmake_minimum_required(VERSION 3.23)
project(Renderer2)

set(REN_INCLUDE ${PROJECT_SOURCE_DIR}/include)
set(REN_INCLUDE_GE $<BUILD_INTERFACE:${REN_INCLUDE}>
                   $<INSTALL_INTERFACE:include>)
set(EXTERNAL_DIR ${PROJECT_SOURCE_DIR}/external)

set(REN_TARGETS RenTargets)
set(REN_VULKAN_TARGETS RenVulkanTargets)
set(REN_DIRECTX12_TARGETS RenDirectX12Targets)

add_subdirectory(lib)

set(BUILD_EXAMPLES
    FALSE
    CACHE BOOL "Build example excutables")
if(${BUILD_EXAMPLES})
  add_subdirectory(examples)
endif()

include(CMakePackageConfigHelpers)
set(CONFIG_CMAKE_IN ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake.in)
set(CONFIG_CMAKE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake)
set(CONFIG_VERSION
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake)
file(
  WRITE ${CONFIG_CMAKE_IN}
  "@PACKAGE_INIT@

include(\${CMAKE_CURRENT_LIST_DIR}/${REN_TARGETS}.cmake)
include(\${CMAKE_CURRENT_LIST_DIR}/${REN_VULKAN_TARGETS}.cmake)
if(WIN32)
  include(\${CMAKE_CURRENT_LIST_DIR}/${REN_DIRECTX12_TARGETS}.cmake)
endif()
check_required_components(${PROJECT_NAME})
")
configure_package_config_file(
  ${CONFIG_CMAKE_IN} ${CONFIG_CMAKE}
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
write_basic_package_version_file(
  ${CONFIG_VERSION}
  VERSION 0.1.0
  COMPATIBILITY SameMinorVersion)
install(FILES ${CONFIG_CMAKE} ${CONFIG_VERSION}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
