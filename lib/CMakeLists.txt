add_library(ren-core
  core/IO.cpp
  core/Profiler.cpp
)
target_include_directories(ren-core PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${REN_INCLUDE})
target_link_libraries(ren-core PUBLIC glm::glm Boost::container fmt::fmt Tracy::TracyClient)
target_compile_features(ren-core PUBLIC cxx_std_23)
add_library(ren::core ALIAS ren-core)

if (REN_ASSERTIONS)
  message(STATUS "Enable assertions")
  target_compile_definitions(ren-core PUBLIC REN_ASSERTIONS)
endif()

add_library(ren-rhi rhi.cpp rhi-vk.cpp rhi-vk-vma.cpp)
target_link_libraries(ren-rhi
  PUBLIC ren::core GPUOpen::VulkanMemoryAllocator tiny_imageformat
  PRIVATE SDL2::SDL2 volk::volk
)
target_compile_definitions(ren-rhi PUBLIC REN_RHI_VULKAN=1)
add_library(ren::rhi ALIAS ren-rhi)

add_library(ren-gfx 
  CommandRecorder.cpp
  DescriptorAllocator.cpp
  FreeListAllocator.cpp
  Pipeline.cpp
  RenderGraph.cpp
  Renderer.cpp
  ResourceUploader.cpp
  Texture.cpp
)
target_link_libraries(ren-gfx
  PUBLIC ren::core ren::rhi
  PRIVATE spirv-reflect KTX::ktx
)
add_library(ren::gfx ALIAS ren-gfx)

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

add_library(ren
  Camera.cpp
  GpuScene.cpp
  Mesh.cpp
  PipelineLoading.cpp
  Scene.cpp
  SwapChain.cpp
  passes/Exposure.cpp
  passes/GpuSceneUpdate.cpp
  passes/HiZ.cpp
  passes/ImGui.cpp
  passes/MeshPass.cpp
  passes/Opaque.cpp
  passes/PostProcessing.cpp
  passes/Present.cpp
  passes/Skybox.cpp
)
target_include_directories(ren PUBLIC ${REN_INCLUDE})
target_link_libraries(ren
  PUBLIC glm::glm tiny_imageformat SDL2::SDL2
  PRIVATE ren::gfx KTX::ktx
)
target_compile_features(ren PUBLIC cxx_std_23)
add_library(ren::ren ALIAS ren)

if (REN_BUILD_IMGUI_PLUGIN)
  message(STATUS "Enable ImGui plugin")
  target_link_libraries(ren PRIVATE imgui::imgui)
  target_compile_definitions(ren PRIVATE REN_IMGUI)
endif()

if (REN_BUILD_SHADER_COMPILER)
  message(STATUS "Build shader compiler")
  add_executable(ren-shader-compiler shader-compiler.cpp)
  target_link_libraries(ren-shader-compiler ren::core glslang::glslang glslang::glslang-default-resource-limits cxxopts::cxxopts spirv-reflect)
  add_executable(ren::shader-compiler ALIAS ren-shader-compiler)
endif()

add_library(ren-baking
  Baking.cpp
  ImageBaking.cpp
  MeshBaking.cpp
  MeshSimplification.cpp
)
target_include_directories(ren-baking PUBLIC ${REN_INCLUDE})
target_link_libraries(ren-baking
  PUBLIC glm::glm tiny_imageformat
  PRIVATE ren-gfx Microsoft::DirectXTex KTX::ktx meshoptimizer::meshoptimizer mikktspace::mikktspace
)
target_compile_features(ren-baking PUBLIC cxx_std_23)
add_library(ren::baking ALIAS ren-baking)

if (REN_BUILD_BAKING_TOOLS)
  message(STATUS "Build baking tools")

  add_executable(ren-bake-ibl bake-ibl.cpp)
  target_link_libraries(ren-bake-ibl ren::core ren::baking cxxopts::cxxopts stb)
endif()

add_subdirectory(glsl)

add_executable(test-sequences test-sequences.cpp)
target_link_libraries(test-sequences ren::core)
