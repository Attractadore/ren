set(REN_LOG_SEVERITY
    "ERROR;WARN;INFO"
    CACHE
      STRING
      "Enable logging for these severity levels (Available levels are: ERROR, WARN, INFO, DEBUG)"
)
option(REN_ASSERTIONS "Enable assertions" OFF)
option(REN_VULKAN_VALIDATION "Enable Vulkan validation layer" OFF)
option(REN_VULKAN_DEBUG_NAMES "Enable Vulkan object debug names" OFF)
option(REN_RENDER_GRAPH_DEBUG_NAMES "Enable RenderGraph object debug names" OFF)

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
find_package(tl-optional REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)
find_package(unofficial-vulkan-memory-allocator REQUIRED)

add_subdirectory(glsl)

add_library(Support INTERFACE)
target_include_directories(Support INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(Support INTERFACE Boost::boost Boost::container fmt::fmt
                                        range-v3::range-v3 tl::optional)
foreach(SEVERITY ${REN_LOG_SEVERITY})
  message(STATUS "Enable ${SEVERITY} logging")
  target_compile_definitions(Support INTERFACE REN_LOG_${SEVERITY})
endforeach()
if (REN_ASSERTIONS)
  message(STATUS "Enable assertions")
  target_compile_definitions(Support INTERFACE REN_ASSERTIONS)
endif()

add_library(Support-PCH INTERFACE)
target_link_libraries(Support-PCH INTERFACE Support)
target_precompile_headers(
  Support-PCH
  INTERFACE
  <array>
  <chrono>
  <deque>
  <functional>
  <memory>
  <optional>
  <iostream>
  <queue>
  <span>
  <tuple>
  <unordered_map>
  <unordered_set>
  <variant>
  <vector>
  <boost/container/flat_map.hpp>
  <boost/container/flat_set.hpp>
  <boost/container/small_vector.hpp>
  <boost/container/static_vector.hpp>
  <boost/functional/hash.hpp>
  <fmt/format.h>
  <range/v3/action.hpp>
  <range/v3/algorithm.hpp>
  <range/v3/range.hpp>
  <range/v3/view.hpp>
  <tl/optional.hpp>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(Support-PCH INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
endif()

set(GEN_DISPATCH_TABLE_PY ${CMAKE_CURRENT_LIST_DIR}/GenDispatchTable.py)
set(VK_XML ${VULKAN_HEADERS_REGISTRY_DIRECTORY}/vk.xml)
set(DISPATCH_TABLE_HPP ${CMAKE_CURRENT_BINARY_DIR}/DispatchTable.hpp)
set(DISPATCH_TABLE_CPP ${CMAKE_CURRENT_BINARY_DIR}/DispatchTable.cpp)

add_custom_command(
  OUTPUT ${DISPATCH_TABLE_HPP} ${DISPATCH_TABLE_CPP}
  DEPENDS ${VK_XML} ${GEN_DISPATCH_TABLE_PY}
  COMMAND Python3::Interpreter ARGS ${GEN_DISPATCH_TABLE_PY} ${VK_XML} ${DISPATCH_TABLE_HPP} ${DISPATCH_TABLE_CPP}
  VERBATIM)

add_library(
  ren
  ren.cpp
  Buffer.cpp
  CommandAllocator.cpp
  CommandRecorder.cpp
  Descriptors.cpp
  Device.cpp
  Formats.cpp
  FreeListAllocator.cpp
  Passes.cpp
  Pipeline.cpp
  PipelineLoading.cpp
  RenderGraph.cpp
  ResourceUploader.cpp
  Scene.cpp
  Swapchain.cpp
  Texture.cpp
  TextureIDAllocator.cpp
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
  PRIVATE Support
          Support-PCH
          glm::glm
          Vulkan::Headers
          unofficial::spirv-reflect::spirv-reflect
          unofficial::vulkan-memory-allocator::vulkan-memory-allocator
          EmbeddedShaders)
target_include_directories(ren PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_precompile_headers(ren PRIVATE <glm/glm.hpp> <spirv_reflect.h>
                          <vk_mem_alloc.h> <vulkan/vulkan.h>)
target_compile_definitions(
  ren PRIVATE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1
              VMA_STATS_STRING_ENABLED=0)
if(REN_VULKAN_VALIDATION)
  message(STATUS "Enable Vulkan validation layer")
  target_compile_definitions(ren PRIVATE REN_VULKAN_VALIDATION)
endif()
if(REN_VULKAN_DEBUG_NAMES)
  message(STATUS "Enable Vulkan object debug names")
  target_compile_definitions(ren PRIVATE REN_DEBUG_NAMES)
endif()
if(REN_RENDER_GRAPH_DEBUG_NAMES)
  message(STATUS "Enable RenderGraph object debug names")
  target_compile_definitions(ren PRIVATE REN_RENDER_GRAPH_DEBUG_NAMES)
endif()

add_subdirectory(Passes)

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
target_link_libraries(ren-hpp INTERFACE ren::ren)
target_compile_features(ren-hpp INTERFACE cxx_std_23)
add_library(ren::ren-hpp ALIAS ren-hpp)

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
