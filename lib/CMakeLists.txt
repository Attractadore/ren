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
find_package(VulkanHeaders REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(fmt REQUIRED)
find_package(range-v3 REQUIRED)
find_package(tl-optional REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)
find_package(volk REQUIRED)

add_library(
  ren
  ren.cpp
  Buffer.cpp
  CommandAllocator.cpp
  CommandRecorder.cpp
  Descriptors.cpp
  Formats.cpp
  FreeListAllocator.cpp
  Passes.cpp
  Pipeline.cpp
  PipelineLoading.cpp
  RenderGraph.cpp
  Renderer.cpp
  ResourceUploader.cpp
  RgBuilder.cpp
  RgPassBuilder.cpp
  Scene.cpp
  Swapchain.cpp
  Texture.cpp
  TextureIdAllocator.cpp
  VMA.cpp)
add_library(ren::ren ALIAS ren)
target_sources(ren PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                          ${REN_INCLUDE}/ren/ren.hpp)
target_link_libraries(
  ren
  PUBLIC glm::glm
  PRIVATE Boost::boost
          Boost::container
          GPUOpen::VulkanMemoryAllocator
          Vulkan::Headers
          fmt::fmt
          range-v3::range-v3
          tl::optional
          unofficial::spirv-reflect::spirv-reflect
          volk::volk)
target_compile_definitions(
  ren PRIVATE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0
              VMA_STATS_STRING_ENABLED=0 VK_NO_PROTOTYPES)
target_compile_features(ren PUBLIC cxx_std_23)
# Clang std::expected fix
if(CMAKE_EXPORT_COMPILE_COMMANDS OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_definitions(ren PUBLIC __cpp_concepts=202002L)
endif()

add_library(ren-vk INTERFACE)
add_library(ren::vk ALIAS ren-vk)
target_sources(ren-vk INTERFACE FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                                ${REN_INCLUDE}/ren/ren-vk.hpp)
target_link_libraries(ren-vk INTERFACE ren::ren Vulkan::Headers)
target_compile_definitions(ren-vk INTERFACE VK_NO_PROTOTYPES)

list(APPEND ren_targets ren)
list(APPEND ren_targets ren-vk)

if(REN_BUILD_SDL2_PLUGIN)
  find_package(SDL2 REQUIRED)
  list(APPEND ren_targets ren-sdl2)
  add_library(ren-sdl2 ren-sdl2.cpp)
  add_library(ren::sdl2 ALIAS ren-sdl2)
  target_sources(ren-sdl2 PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                 FILES ${REN_INCLUDE}/ren/ren-sdl2.hpp)
  target_link_libraries(
    ren-sdl2
    PUBLIC ren::ren SDL2::SDL2
    PRIVATE ren::vk)
endif()

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

add_subdirectory(glsl)
add_subdirectory(Passes)

if(REN_INSTALL)
  include(GNUInstallDirs)
  install(
    TARGETS ${ren_targets}
    EXPORT ${REN_TARGETS_EXPORT_NAME}
    FILE_SET HEADERS)
  install(
    EXPORT ${REN_TARGETS_EXPORT_NAME}
    NAMESPACE ren::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
