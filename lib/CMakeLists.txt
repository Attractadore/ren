add_library(ren-core
  core/IO.cpp
  core/Profiler.cpp
)
target_include_directories(ren-core PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${REN_INCLUDE})
target_link_libraries(ren-core PUBLIC glm::glm Boost::container fmt::fmt Tracy::TracyClient)
target_compile_features(ren-core PUBLIC cxx_std_23)
target_compile_definitions(ren-core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
add_library(ren::core ALIAS ren-core)

if (REN_ASSERTIONS)
  message(STATUS "Enable assertions")
  target_compile_definitions(ren-core PUBLIC REN_ASSERTIONS)
endif()

if (REN_HOT_RELOAD)
  add_library(ren-vma SHARED vma.cpp)
else()
  add_library(ren-vma vma.cpp)
endif()
target_link_libraries(ren-vma PUBLIC GPUOpen::VulkanMemoryAllocator Vulkan::Headers)

add_library(ren-rhi rhi.cpp rhi-vk.cpp)
target_link_libraries(ren-rhi
  PUBLIC ren::core ren-vma Vulkan::Headers tiny_imageformat
  PRIVATE SDL2::SDL2 volk::volk
)
add_library(ren::rhi ALIAS ren-rhi)

add_library(ren-internal 
  CommandRecorder.cpp
  DescriptorAllocator.cpp
  FreeListAllocator.cpp
  Pipeline.cpp
  RenderGraph.cpp
  Renderer.cpp
  ResourceUploader.cpp
  Texture.cpp
)
target_link_libraries(ren-internal
  PUBLIC ren::core ren::rhi
  PRIVATE spirv-reflect KTX::ktx
)

if(REN_DEBUG_LAYER)
  message(STATUS "Enable RHI debug layer")
  target_compile_definitions(ren-internal PUBLIC REN_DEBUG_LAYER)
endif()
if(REN_DEBUG_NAMES)
  message(STATUS "Enable RHI object debug names")
  target_compile_definitions(ren-internal PUBLIC REN_DEBUG_NAMES)
endif()
if(REN_RENDER_GRAPH_DEBUG)
  message(STATUS "Enable render graph debug features")
  target_compile_definitions(ren-internal PUBLIC REN_RG_DEBUG)
endif()
if(REN_HOT_RELOAD)
  message(STATUS "Enable hot reload support")
  target_compile_definitions(ren-core PUBLIC REN_HOT_RELOAD)
endif()

add_library(ren STATIC 
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
  PRIVATE ren-internal KTX::ktx
)
target_compile_features(ren PUBLIC cxx_std_23)

if(REN_HOT_RELOAD)
  add_library(libren MODULE libren.cpp)
  set_property(TARGET libren PROPERTY PREFIX "")
  target_link_libraries(libren PUBLIC ren PRIVATE ren-internal)

  add_library(ren-hot-reload STATIC hot-reload.cpp)
  target_include_directories(ren-hot-reload PUBLIC ${REN_INCLUDE})
  target_link_libraries(ren-hot-reload PUBLIC glm::glm tiny_imageformat PRIVATE ren::core SDL2::SDL2)
  target_compile_features(ren-hot-reload PUBLIC cxx_std_23)

  target_compile_definitions(ren-hot-reload
    PUBLIC REN_HOT_RELOAD
    PRIVATE
    REN_LIB_DIR="$<TARGET_FILE_DIR:libren>"
    REN_LIB_NAME="$<TARGET_FILE_NAME:libren>"
    REN_LIB_PATH="$<TARGET_FILE:libren>"
  )

  add_library(ren::ren ALIAS ren-hot-reload)
else()
  add_library(ren::ren ALIAS ren)
endif()

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
  ImageBaking.cpp
  MeshBaking.cpp
  MeshSimplification.cpp
)
target_include_directories(ren-baking PUBLIC ${REN_INCLUDE})
target_link_libraries(ren-baking
  PUBLIC glm::glm tiny_imageformat
  PRIVATE ren::core Microsoft::DirectXTex KTX::ktx meshoptimizer::meshoptimizer mikktspace::mikktspace
)
target_compile_features(ren-baking PUBLIC cxx_std_23)
add_library(ren::baking ALIAS ren-baking)

if (REN_BUILD_BAKING_TOOLS)
  message(STATUS "Build baking tools")

  add_library(ren-baking-internal Baking.cpp)
  target_link_libraries(ren-baking-internal PUBLIC ren-internal ren-baking)

  add_executable(ren-bake-ibl bake-ibl.cpp)
  target_link_libraries(ren-bake-ibl ren-baking-internal Microsoft::DirectXTex KTX::ktx cxxopts::cxxopts stb)
endif()

add_subdirectory(glsl)

add_executable(test-sequences test-sequences.cpp)
target_link_libraries(test-sequences ren::core)
