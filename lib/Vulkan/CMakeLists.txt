set(VULKAN_HEADERS ${PROJECT_SOURCE_DIR}/external/Vulkan-Headers)
add_subdirectory(${VULKAN_HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/Vulkan-Headers)
add_subdirectory(generate)

add_library(Ren-Vulkan SHARED RenVulkan.cpp VulkanDevice.cpp)
target_include_directories(Ren-Vulkan PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(
  Ren-Vulkan
  PUBLIC Ren
  PRIVATE VulkanDispatchTable Support Vulkan::Headers)
