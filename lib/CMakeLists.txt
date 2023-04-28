set(REN_LOG_SEVERITY
    "ERROR;WARN;INFO"
    CACHE
      STRING
      "Enable logging for these severity levels (Available levels are: ERROR, WARN, INFO, DEBUG)"
)
option(REN_BUILD_TESTS "Build tests" OFF)
option(REN_VULKAN_VALIDATION "Enable Vulkan validation layer" OFF)

find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)
find_package(VulkanHeaders REQUIRED)
target_compile_definitions(Vulkan::Headers INTERFACE VK_NO_PROTOTYPE)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(range-v3 REQUIRED)
find_package(tl-expected REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)
find_package(unofficial-vulkan-memory-allocator REQUIRED)

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(Support INTERFACE Boost::boost Boost::container SlotMap
                                        fmt::fmt range-v3::range-v3)
foreach(SEVERITY ${REN_LOG_SEVERITY})
  message(STATUS "Enable ${SEVERITY} logging")
  target_compile_definitions(Support INTERFACE REN_LOG_${SEVERITY})
endforeach()

add_library(Support-PCH INTERFACE)
target_link_libraries(Support-PCH INTERFACE Support)
target_precompile_headers(
  Support-PCH
  INTERFACE
  <array>
  <functional>
  <memory>
  <optional>
  <queue>
  <span>
  <tuple>
  <unordered_map>
  <variant>
  <vector>
  <boost/container/small_vector.hpp>
  <boost/container/static_vector.hpp>
  <fmt/format.h>
  <glm/glm.hpp>
  <range/v3/action.hpp>
  <range/v3/algorithm.hpp>
  <range/v3/range.hpp>
  <range/v3/view.hpp>)

file(
  CONFIGURE
  OUTPUT
  GeneratedConfig.hpp
  CONTENT
  "#pragma once
namespace ren {
constexpr const char* c_assets_directory = \"${REN_ASSETS_OUTPUT_DIRECTORY}\";
}
")

set(GEN_DISPATCH_TABLE_PY ${CMAKE_CURRENT_LIST_DIR}/GenDispatchTable.py)
set(VULKAN_SPEC_XML
    $<TARGET_PROPERTY:Vulkan::Registry,INTERFACE_INCLUDE_DIRECTORIES>/vk.xml)
set(DISPATCH_TABLE_H ${CMAKE_CURRENT_BINARY_DIR}/DispatchTable.h)
set(DISPATCH_TABLE_HPP ${CMAKE_CURRENT_BINARY_DIR}/DispatchTable.hpp)
set(DISPATCH_TABLE_CPP ${CMAKE_CURRENT_BINARY_DIR}/DispatchTable.cpp)

add_custom_command(
  OUTPUT ${DISPATCH_TABLE_H} ${DISPATCH_TABLE_HPP} ${DISPATCH_TABLE_CPP}
  DEPENDS ${VULKAN_SPEC_XML} ${GEN_DISPATCH_TABLE_PY}
  COMMAND Python3::Interpreter ARGS ${GEN_DISPATCH_TABLE_PY} ${VULKAN_SPEC_XML}
          ${DISPATCH_TABLE_H} ${DISPATCH_TABLE_HPP} ${DISPATCH_TABLE_CPP}
  VERBATIM)

add_library(
  ren
  ren.cpp
  AssetLoader.cpp
  CommandAllocator.cpp
  CommandBuffer.cpp
  DescriptorSetAllocator.cpp
  Device.cpp
  Formats.cpp
  MaterialPipelineCompiler.cpp
  Pipeline.cpp
  Reflection.cpp
  RenderGraph.cpp
  ResourceUploader.cpp
  Scene.cpp
  Swapchain.cpp
  Texture.cpp
  VMA.cpp
  ${DISPATCH_TABLE_CPP})
target_sources(
  ren
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren.h
         ${REN_INCLUDE}/ren/ren-vk.h)
target_link_libraries(
  ren
  PRIVATE Support Support-PCH glm::glm Vulkan::Headers
          unofficial::spirv-reflect::spirv-reflect
          unofficial::vulkan-memory-allocator::vulkan-memory-allocator)
target_include_directories(ren PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_precompile_headers(ren PRIVATE <glm/glm.hpp> <spirv_reflect.h>
                          <vk_mem_alloc.h> <vulkan/vulkan.h>)
if(REN_VULKAN_VALIDATION)
  message(STATUS "Enable Vulkan validation layer")
  target_compile_definitions(ren PRIVATE REN_VULKAN_VALIDATION)
endif()

if(BUILD_SHARED_LIBS)
  add_library(ren::ren ALIAS ren)
else()
  include(BundleDependencies)
  bundle_dependencies(ren ren-bundled)
  set_target_properties(ren-bundled PROPERTIES EXPORT_NAME "ren")
  add_library(ren::ren ALIAS ren-bundled)
endif()

add_library(ren-hpp INTERFACE)
target_sources(
  ren-hpp
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren.hpp
         ${REN_INCLUDE}/ren/ren-vk.hpp)
target_link_libraries(ren-hpp INTERFACE ren::ren tl::expected)
target_compile_features(ren-hpp INTERFACE cxx_std_20)
add_library(ren::ren-hpp ALIAS ren-hpp)

add_subdirectory(hlsl)

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
