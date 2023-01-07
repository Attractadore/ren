find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)

set(GEN_DISPATCH_TABLE_PY ${CMAKE_CURRENT_LIST_DIR}/GenDispatchTable.py)
set(VULKAN_SPEC_XML
    $<TARGET_PROPERTY:Vulkan::Registry,INTERFACE_INCLUDE_DIRECTORIES>/vk.xml)
set(VULKAN_DISPATCH_TABLE_H ${CMAKE_CURRENT_BINARY_DIR}/VulkanDispatchTable.h)
set(VULKAN_DISPATCH_TABLE_HPP
    ${CMAKE_CURRENT_BINARY_DIR}/VulkanDispatchTable.hpp)
set(VULKAN_DISPATCH_TABLE_C ${CMAKE_CURRENT_BINARY_DIR}/VulkanDispatchTable.c)

add_custom_command(
  OUTPUT ${VULKAN_DISPATCH_TABLE_H} ${VULKAN_DISPATCH_TABLE_HPP}
         ${VULKAN_DISPATCH_TABLE_C}
  DEPENDS ${VULKAN_SPEC_XML} ${GEN_DISPATCH_TABLE_PY}
  COMMAND
    Python3::Interpreter ARGS ${GEN_DISPATCH_TABLE_PY} ${VULKAN_SPEC_XML}
    ${VULKAN_DISPATCH_TABLE_H} ${VULKAN_DISPATCH_TABLE_HPP}
    ${VULKAN_DISPATCH_TABLE_C}
  VERBATIM)
add_library(VulkanDispatchTable STATIC ${VULKAN_DISPATCH_TABLE_C})
target_include_directories(VulkanDispatchTable
                           PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(VulkanDispatchTable PUBLIC Vulkan::Headers)
target_compile_definitions(VulkanDispatchTable PUBLIC VK_NO_PROTOTYPE)
