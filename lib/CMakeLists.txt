option(REN_RENDER_GRAPH_DEBUG "Enable render graph debug features" OFF)
option(REN_ASSERTIONS "Enable assertions" OFF)
option(REN_DEBUG_LAYER "Enable RHI debug layer" OFF)
option(REN_DEBUG_NAMES "Enable RHI object debug names" OFF)

find_package(Boost COMPONENTS container REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(tl-optional REQUIRED)

add_library(ren-core core/Profiler.cpp)
target_link_libraries(ren-core PUBLIC
  glm::glm
  Boost::boost
  Boost::container
  fmt::fmt
  tl::optional
  Tracy::TracyClient
)
target_compile_features(ren-core PUBLIC cxx_std_23)

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

add_library(ren-gfx 
  CommandRecorder.cpp
  DescriptorAllocator.cpp
  FreeListAllocator.cpp
  Pipeline.cpp
  RenderGraph.cpp
  Renderer.cpp
  Texture.cpp
)
target_link_libraries(ren-gfx PUBLIC ren-core ren-rhi PRIVATE unofficial::spirv-reflect)

add_library(ren
  Camera.cpp
  GpuScene.cpp
  Mesh.cpp
  PipelineLoading.cpp
  ResourceUploader.cpp
  Scene.cpp
  Swapchain.cpp
  passes/Exposure.cpp
  passes/GpuSceneUpdate.cpp
  passes/HiZ.cpp
  passes/ImGui.cpp
  passes/MeshPass.cpp
  passes/Opaque.cpp
  passes/PostProcessing.cpp
  passes/Present.cpp
  passes/Skybox.cpp)
add_library(ren::ren ALIAS ren)
target_sources(ren PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES ${REN_INCLUDE}/ren/ren.hpp)
target_link_libraries(ren
  PUBLIC glm::glm tiny_imageformat SDL2::SDL2
  PRIVATE ren-gfx)
target_compile_features(ren PUBLIC cxx_std_23)

if(REN_BUILD_IMGUI_PLUGIN)
  message(STATUS "Enable ImGui plugin")
  target_compile_definitions(ren-gfx PUBLIC REN_IMGUI)
  target_link_libraries(ren-gfx PUBLIC imgui::imgui)
  add_library(ren-imgui ren-imgui.cpp)
  add_library(ren::imgui ALIAS ren-imgui)
  target_sources(ren-imgui PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES ${REN_INCLUDE}/ren/ren-imgui.hpp)
  target_link_libraries(ren-imgui
    PUBLIC ren::ren imgui::imgui
    PRIVATE ren-gfx)
endif()

if(REN_ASSERTIONS)
  message(STATUS "Enable assertions")
  target_compile_definitions(ren-gfx PUBLIC REN_ASSERTIONS)
endif()
if(REN_DEBUG_LAYER)
  message(STATUS "Enable RHI debug layer")
  target_compile_definitions(ren-gfx PUBLIC REN_DEBUG_LAYER)
endif()
if(REN_DEBUG_NAMES)
  message(STATUS "Enable RHI object debug names")
  target_compile_definitions(ren-gfx PUBLIC REN_DEBUG_NAMES)
endif()
if(REN_RENDER_GRAPH_DEBUG)
  message(STATUS "Enable render graph debug features")
  target_compile_definitions(ren-gfx PUBLIC REN_RG_DEBUG)
endif()

if (REN_BUILD_SHADER_COMPILER)
  message(STATUS "Build shader compiler")
  add_subdirectory(shader-compiler)
endif()

find_package(Ktx REQUIRED)
find_package(directxtex REQUIRED)
find_package(meshoptimizer REQUIRED)
find_package(mikktspace REQUIRED)

add_library(ren-baking
  Baking.cpp
  ImageBaking.cpp
  MeshBaking.cpp
  MeshSimplification.cpp
)
add_library(ren::baking ALIAS ren-baking)
target_sources(
  ren-baking PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
  FILES
  ${REN_INCLUDE}/ren/baking/baking.hpp
  ${REN_INCLUDE}/ren/baking/image.hpp
  ${REN_INCLUDE}/ren/baking/mesh.hpp
)
target_link_libraries(ren-baking
  PUBLIC glm::glm tiny_imageformat
  PRIVATE ren-gfx Microsoft::DirectXTex KTX::ktx meshoptimizer::meshoptimizer mikktspace::mikktspace)
target_compile_features(ren-baking PUBLIC cxx_std_23)

if (REN_BUILD_BAKING_TOOLS)
  message(STATUS "Build baking tools")

  find_package(cxxopts REQUIRED)

  add_executable(ren-bake-dhr-lut bake-dhr-lut.cpp)
  target_link_libraries(ren-bake-dhr-lut ren-baking cxxopts::cxxopts)
endif()

add_subdirectory(glsl)
