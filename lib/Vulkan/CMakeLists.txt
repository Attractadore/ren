set(VULKAN_HEADERS_DIR ${EXTERNAL_DIR}/Vulkan-Headers)
set(VMA_DIR ${EXTERNAL_DIR}/VulkanMemoryAllocator)
set(REN_VULKAN_VALIDATION
    TRUE
    CACHE BOOL "Enable Vulkan validation layer")

add_subdirectory(${VULKAN_HEADERS_DIR}
                 ${CMAKE_CURRENT_BINARY_DIR}/Vulkan-Headers)

add_subdirectory(generate)

add_library(vma STATIC vma.cpp)
target_include_directories(vma PUBLIC ${VMA_DIR}/include ${VMA_DIR}/src
                                      ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(vma PUBLIC Vulkan::Headers)
target_compile_features(vma PRIVATE cxx_std_17)

add_library(
  ren-vk SHARED
  ren-vk.cpp
  VulkanCommandAllocator.cpp
  VulkanCommandBuffer.cpp
  VulkanCommandPool.cpp
  VulkanDevice.cpp
  VulkanRenderGraph.cpp
  VulkanSwapchain.cpp)
target_include_directories(ren-vk PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(
  ren-vk
  PUBLIC ren
  PRIVATE VulkanDispatchTable Support vma)
target_compile_definitions(ren-vk PRIVATE)
if(REN_VULKAN_VALIDATION)
  message(STATUS "Enable Vulkan validation layer")
  target_compile_definitions(ren-vk PRIVATE REN_VULKAN_VALIDATION)
endif()

target_sources(
  ren-vk
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren-vk.h
         ${REN_INCLUDE}/ren/ren-vk.hpp)
set_target_properties(ren-vk PROPERTIES EXPORT_NAME "vk")
add_library(ren::vk ALIAS ren-vk)

include(GNUInstallDirs)
install(
  TARGETS ren-vk
  EXPORT ${REN_VULKAN_TARGETS}
  FILE_SET HEADERS)
install(
  EXPORT ${REN_VULKAN_TARGETS}
  NAMESPACE ren::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
