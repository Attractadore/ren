option(REN_RENDER_GRAPH_DEBUG "Enable render graph debug features" OFF)
option(REN_BUILD_SDL2_PLUGIN "Enable SDL2 plugin for simpler initialization and swapchain creation when using SDL2" OFF)
option(REN_BUILD_IMGUI_PLUGIN "Enable ImGui plugin for debug UI rendering" OFF)
option(REN_BUILD_SHADER_COMPILER "Build shader compiler" ON)
option(REN_PROFILER "Enable profiler" OFF)
option(REN_ASSERTIONS "Enable assertions" OFF)
option(REN_VULKAN_VALIDATION "Enable Vulkan validation layer" OFF)
option(REN_VULKAN_DEBUG_NAMES "Enable Vulkan object debug names" OFF)

find_package(Boost COMPONENTS container REQUIRED)
find_package(VulkanHeaders REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(meshoptimizer REQUIRED)
find_package(mikktspace REQUIRED)
find_package(tl-optional REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)
find_package(volk REQUIRED)

add_library(ren-core INTERFACE)
target_link_libraries(ren-core INTERFACE
  glm::glm
  Boost::boost
  Boost::container
  fmt::fmt
  tl::optional)

add_library(ren-gfx INTERFACE)
target_link_libraries(ren-gfx INTERFACE
  ren-core
  GPUOpen::VulkanMemoryAllocator
  meshoptimizer::meshoptimizer
  mikktspace::mikktspace
  unofficial::spirv-reflect
  volk::volk)
target_compile_definitions(ren-gfx INTERFACE
  VMA_STATIC_VULKAN_FUNCTIONS=0
  VMA_DYNAMIC_VULKAN_FUNCTIONS=0
  VMA_STATS_STRING_ENABLED=0 VK_NO_PROTOTYPES)

add_library(ren
  ren.cpp
  BumpAllocator.cpp
  Camera.cpp
  CommandAllocator.cpp
  CommandRecorder.cpp
  DescriptorAllocator.cpp
  Descriptors.cpp
  Formats.cpp
  FreeListAllocator.cpp
  GpuScene.cpp
  Mesh.cpp
  MeshProcessing.cpp
  MeshSimplification.cpp
  Pipeline.cpp
  PipelineLoading.cpp
  Profiler.cpp
  RenderGraph.cpp
  Renderer.cpp
  ResourceUploader.cpp
  Scene.cpp
  Swapchain.cpp
  Texture.cpp
  VMA.cpp
  passes/Exposure.cpp
  passes/GpuSceneUpdate.cpp
  passes/HiZ.cpp
  passes/ImGui.cpp
  passes/MeshPass.cpp
  passes/Opaque.cpp
  passes/PostProcessing.cpp
  passes/Present.cpp)
add_library(ren::ren ALIAS ren)
target_sources(ren PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES ${REN_INCLUDE}/ren/ren.hpp)
target_link_libraries(ren
  PUBLIC glm::glm tiny_imageformat
  PRIVATE ren-gfx)
target_compile_features(ren PUBLIC cxx_std_23)

if(REN_BUILD_SDL2_PLUGIN)
  message(STATUS "Enable SDL2 plugin")
  find_package(SDL2 REQUIRED)
  add_library(ren-sdl2 ren-sdl2.cpp)
  add_library(ren::sdl2 ALIAS ren-sdl2)
  target_sources(ren-sdl2 PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES ${REN_INCLUDE}/ren/ren-sdl2.hpp)
  target_link_libraries(ren-sdl2
    PUBLIC ren::ren SDL2::SDL2
    PRIVATE ren-gfx)
endif()

if(REN_BUILD_IMGUI_PLUGIN)
  message(STATUS "Enable ImGui plugin")
  find_package(ImGui REQUIRED)
  target_compile_definitions(ren-gfx INTERFACE REN_IMGUI)
  target_link_libraries(ren-gfx INTERFACE imgui::imgui)
  add_library(ren-imgui ren-imgui.cpp)
  add_library(ren::imgui ALIAS ren-imgui)
  target_sources(ren-imgui PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES ${REN_INCLUDE}/ren/ren-imgui.hpp)
  target_link_libraries(ren-imgui
    PUBLIC ren::ren imgui::imgui
    PRIVATE ren-gfx)
endif()

if(REN_PROFILER)
  message(STATUS "Enable profiler")
  find_package(Tracy REQUIRED)
  target_link_libraries(ren-gfx INTERFACE Tracy::TracyClient)
  target_compile_definitions(ren-gfx INTERFACE REN_PROFILER)
endif()
if(REN_ASSERTIONS)
  message(STATUS "Enable assertions")
  target_compile_definitions(ren-gfx INTERFACE REN_ASSERTIONS)
endif()
if(REN_VULKAN_VALIDATION)
  message(STATUS "Enable Vulkan validation layer")
  target_compile_definitions(ren-gfx INTERFACE REN_VULKAN_VALIDATION)
endif()
if(REN_VULKAN_DEBUG_NAMES)
  message(STATUS "Enable Vulkan object debug names")
  target_compile_definitions(ren-gfx INTERFACE REN_DEBUG_NAMES)
endif()
if(REN_RENDER_GRAPH_DEBUG)
  message(STATUS "Enable render graph debug features")
  target_compile_definitions(ren-gfx INTERFACE REN_RG_DEBUG)
endif()

add_subdirectory(glsl)

if (REN_BUILD_SHADER_COMPILER)
  message(STATUS "Build shader compiler")
  add_subdirectory(shader-compiler)
endif()
