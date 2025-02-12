option(REN_RENDER_GRAPH_DEBUG "Enable render graph debug features" OFF)
option(REN_ASSERTIONS "Enable assertions" OFF)
option(REN_DEBUG_LAYER "Enable RHI debug layer" OFF)
option(REN_DEBUG_NAMES "Enable RHI object debug names" OFF)

find_package(Boost COMPONENTS container REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(tl-optional REQUIRED)

add_library(ren-core INTERFACE)
target_link_libraries(ren-core INTERFACE
  glm::glm
  Boost::boost
  Boost::container
  fmt::fmt
  tl::optional)
target_compile_features(ren-core INTERFACE cxx_std_23)

find_package(VulkanHeaders REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(unofficial-spirv-reflect REQUIRED)
find_package(volk REQUIRED)

add_library(ren-rhi rhi-vk.cpp rhi-vk-vma.cpp)
target_link_libraries(ren-rhi PUBLIC 
  ren-core
  GPUOpen::VulkanMemoryAllocator
  tiny_imageformat
  unofficial::spirv-reflect
  volk::volk)
target_compile_definitions(ren-rhi PUBLIC REN_RHI_VULKAN=1 VK_NO_PROTOTYPES)

find_package(meshoptimizer REQUIRED)
find_package(mikktspace REQUIRED)

add_library(ren-gfx INTERFACE)
target_link_libraries(ren-gfx INTERFACE
  ren-core
  ren-rhi
  meshoptimizer::meshoptimizer
  mikktspace::mikktspace)

add_library(ren
  ren.cpp
  Camera.cpp
  CommandRecorder.cpp
  DescriptorAllocator.cpp
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
  PUBLIC glm::glm tiny_imageformat SDL2::SDL2
  PRIVATE ren-gfx)
target_compile_features(ren PUBLIC cxx_std_23)

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
if(REN_DEBUG_LAYER)
  message(STATUS "Enable RHI debug layer")
  target_compile_definitions(ren-gfx INTERFACE REN_DEBUG_LAYER)
endif()
if(REN_DEBUG_NAMES)
  message(STATUS "Enable RHI object debug names")
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
