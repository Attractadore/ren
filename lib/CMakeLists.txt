set(REN_LOG_SEVERITY
    "ERROR;WARN;INFO"
    CACHE
      STRING
      "Enable logging for these severity levels (Available levels are: ERROR, WARN, INFO, DEBUG)"
)
option(REN_ASSERTIONS "Enable assertions" OFF)
option(REN_VULKAN_VALIDATION "Enable Vulkan validation layer" OFF)
option(REN_VULKAN_DEBUG_NAMES "Enable Vulkan object debug names" OFF)
option(REN_RENDER_GRAPH_DEBUG "Enable render graph debug features" OFF)
option(REN_PCH "Enable precompiled header" OFF)

find_package(
  Boost
  COMPONENTS container
  REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(range-v3 REQUIRED)
find_package(tl-optional REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)
find_package(volk REQUIRED)
find_package(VulkanHeaders REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)

add_subdirectory(glsl)

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
  RgBuilder.cpp
  RgPassBuilder.cpp
  Scene.cpp
  Swapchain.cpp
  Texture.cpp
  TextureIdAllocator.cpp
  VMA.cpp)
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
  PRIVATE Boost::boost
          Boost::container
          GPUOpen::VulkanMemoryAllocator
          Vulkan::Headers
          fmt::fmt
          glm::glm
          range-v3::range-v3
          tl::optional
          unofficial::spirv-reflect::spirv-reflect
          volk::volk
          EmbeddedShaders)
if(REN_PCH)
  message(STATUS "Enable precompiled header")
  target_precompile_headers(
    ren
    PRIVATE
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
    <tl/optional.hpp>
    <glm/glm.hpp>
    <spirv_reflect.h>
    <vk_mem_alloc.h>
    <vulkan/vulkan.h>
    <volk.h>)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(
      ren
      INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
  endif()
endif()
target_compile_definitions(
  ren PRIVATE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0
              VMA_STATS_STRING_ENABLED=0)
target_compile_definitions(ren PUBLIC VK_NO_PROTOTYPES)
foreach(SEVERITY ${REN_LOG_SEVERITY})
  message(STATUS "Enable ${SEVERITY} logging")
  target_compile_definitions(ren PRIVATE REN_LOG_${SEVERITY})
endforeach()
if(REN_ASSERTIONS)
  message(STATUS "Enable assertions")
  target_compile_definitions(ren PRIVATE REN_ASSERTIONS)
endif()
if(REN_VULKAN_VALIDATION)
  message(STATUS "Enable Vulkan validation layer")
  target_compile_definitions(ren PRIVATE REN_VULKAN_VALIDATION)
endif()
if(REN_VULKAN_DEBUG_NAMES)
  message(STATUS "Enable Vulkan object debug names")
  target_compile_definitions(ren PRIVATE REN_DEBUG_NAMES)
endif()
if(REN_RENDER_GRAPH_DEBUG)
  message(STATUS "Enable render graph debug features")
  target_compile_definitions(ren PRIVATE REN_RG_DEBUG)
endif()

add_subdirectory(Passes)

add_library(ren::ren ALIAS ren)

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
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  # clangd std::expected fix
  target_compile_definitions(ren-hpp INTERFACE __cpp_concepts=202002L)
endif()
add_library(ren::ren-hpp ALIAS ren-hpp)

if(REN_INSTALL)
  include(GNUInstallDirs)
  install(
    TARGETS ren ren-hpp
    EXPORT ${REN_TARGETS}
    FILE_SET HEADERS)
  install(
    EXPORT ${REN_TARGETS}
    NAMESPACE ren::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
