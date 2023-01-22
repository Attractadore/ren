option(REN_VULKAN_VALIDATION "Enable Vulkan validation layer" OFF)

find_package(unofficial-vulkan-memory-allocator REQUIRED)

add_subdirectory(DispatchTable)

add_library(ren-vk ren-vk.cpp VMA.cpp VulkanDevice.cpp VulkanCommandPool.cpp
                   VulkanRenderGraph.cpp VulkanSwapchain.cpp)
target_sources(ren-vk PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                             ${REN_INCLUDE}/ren/ren-vk.h)
target_precompile_headers(ren-vk PRIVATE <vk_mem_alloc.h> <vulkan/vulkan.h>)
target_link_libraries(
  ren-vk
  PUBLIC ren::ren
  PRIVATE Support Support-PCH VulkanDispatchTable
          unofficial::vulkan-memory-allocator::vulkan-memory-allocator)
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

add_library(ren-vk-hpp INTERFACE)
target_sources(ren-vk-hpp PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                 FILES ${REN_INCLUDE}/ren/ren-vk.hpp)
target_link_libraries(ren-vk-hpp INTERFACE ren::ren-hpp ren::vk)
set_target_properties(ren-vk-hpp PROPERTIES EXPORT_NAME "vk-hpp")
add_library(ren::vk-hpp ALIAS ren-vk-hpp)

if(REN_INSTALL)
  include(GNUInstallDirs)
  get_target_property(ren_vk_target ren::vk ALIASED_TARGET)
  install(
    TARGETS ${ren_vk_target} ren-vk-hpp
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
