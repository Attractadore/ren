set(VULKAN_HEADERS_DIR ${EXTERNAL_DIR}/Vulkan-Headers)
set(VMA_DIR ${EXTERNAL_DIR}/VulkanMemoryAllocator)

add_subdirectory(${VULKAN_HEADERS_DIR} ${CMAKE_CURRENT_BINARY_DIR}/Vulkan-Headers)

add_subdirectory(generate)

add_library(vma STATIC vma.cpp)
target_include_directories(vma PUBLIC ${VMA_DIR}/include ${VMA_DIR}/src ${REN_PRIVATE_INCLUDE_DIR})
target_link_libraries(vma PUBLIC Vulkan::Headers)
target_compile_features(vma PRIVATE cxx_std_17)

add_library(ren-vk SHARED ren-vk.cpp
  VulkanCommandAllocator.cpp
  VulkanCommandBuffer.cpp
  VulkanCommandPool.cpp
  VulkanDevice.cpp
  VulkanRenderGraph.cpp
  VulkanSwapchain.cpp
  VulkanTexture.cpp
)
target_include_directories(ren-vk PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(
  ren-vk
  PUBLIC ren
  PRIVATE VulkanDispatchTable Support vma)
