option(REN_VULKAN_VALIDATION "Enable Vulkan validation layer" OFF)

set(VULKAN_HEADERS_DIR ${EXTERNAL_DIR}/Vulkan-Headers)
add_subdirectory(${VULKAN_HEADERS_DIR} Vulkan-Headers)

add_subdirectory(generate)

set(VMA_DIR ${EXTERNAL_DIR}/VulkanMemoryAllocator)
add_library(VMA STATIC VMA.cpp)
target_include_directories(VMA PUBLIC ${VMA_DIR}/include ${VMA_DIR}/src
                                      ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(VMA PUBLIC Vulkan::Headers)
target_compile_features(VMA PRIVATE cxx_std_17)

add_library(
  ren-vk
  ren-vk.cpp
  VulkanCommandAllocator.cpp
  VulkanCommandBuffer.cpp
  VulkanCommandPool.cpp
  VulkanDevice.cpp
  VulkanRenderGraph.cpp
  VulkanSwapchain.cpp)
target_sources(
  ren-vk
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren-vk.h
         ${REN_INCLUDE}/ren/ren-vk.hpp)
target_link_libraries(
  ren-vk
  PUBLIC ren::ren
  PRIVATE VulkanDispatchTable Support VMA)
target_compile_definitions(ren-vk PRIVATE)
if(REN_VULKAN_VALIDATION)
  message(STATUS "Enable Vulkan validation layer")
  target_compile_definitions(ren-vk PRIVATE REN_VULKAN_VALIDATION)
endif()

if(BUILD_SHARED_LIBS)
  set_target_properties(ren-vk PROPERTIES EXPORT_NAME "vk")
  add_library(ren::vk ALIAS ren-vk)
else()
  include(BundleDependencies)
  bundle_dependencies(ren-vk ren-vk-bundled)
  set_target_properties(ren-vk-bundled PROPERTIES EXPORT_NAME "vk")
  add_library(ren::vk ALIAS ren-vk-bundled)
endif()

if(REN_INSTALL)
  include(GNUInstallDirs)
  get_target_property(ren_vk_target ren::vk ALIASED_TARGET)
  install(
    TARGETS ${ren_vk_target}
    EXPORT ${REN_VULKAN_TARGETS}
    FILE_SET HEADERS)
  if(NOT BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PROPERTY:${ren_vk_target},ARCHIVE_OUTPUT_NAME>
            TYPE LIB)
  endif()
  install(
    EXPORT ${REN_VULKAN_TARGETS}
    NAMESPACE ren::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
