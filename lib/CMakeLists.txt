include(GenerateExportHeader)

add_library(ren-core
  core/Arena.cpp
  core/CmdLine.cpp
  core/IO.cpp
  core/LinuxVm.cpp
  core/Profiler.cpp
  core/Win32Vm.cpp
)
target_include_directories(ren-core PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${REN_INCLUDE})
target_link_libraries(ren-core PUBLIC glm::glm fmt::fmt Tracy::TracyClient)
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
generate_export_header(ren-vma)
target_link_libraries(ren-vma PUBLIC ren::core GPUOpen::VulkanMemoryAllocator Vulkan::Headers)
target_include_directories(ren-vma PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
add_library(ren::vma ALIAS ren-vma)

add_library(ren-rhi rhi.cpp rhi-vk.cpp)
target_link_libraries(ren-rhi
  PUBLIC ren::core ren::vma Vulkan::Headers tiny_imageformat
  PRIVATE SDL3::SDL3 volk::volk
)
add_library(ren::rhi ALIAS ren-rhi)

add_library(ren-internal 
  CommandRecorder.cpp
  DescriptorAllocator.cpp
  Pipeline.cpp
  RenderGraph.cpp
  Renderer.cpp
  ResourceArena.cpp
  ResourceUploader.cpp
  Texture.cpp
)
target_link_libraries(ren-internal
  PUBLIC ren::core ren::rhi
  PRIVATE SPIRV-Headers KTX::ktx
)

if(REN_DEBUG_LAYER)
  message(STATUS "Enable RHI debug layer")
  target_compile_definitions(ren-internal PUBLIC REN_DEBUG_LAYER)
endif()
if(REN_HOT_RELOAD)
  message(STATUS "Enable hot reload support")
  target_compile_definitions(ren-core PUBLIC REN_HOT_RELOAD)
endif()

add_library(ren STATIC 
  Camera.cpp
  PipelineLoading.cpp
  Scene.cpp
  SwapChain.cpp
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
  PUBLIC glm::glm tiny_imageformat SDL3::SDL3
  PRIVATE ren-internal KTX::ktx
)
target_compile_features(ren PUBLIC cxx_std_23)

if(REN_HOT_RELOAD)
  add_library(libren MODULE libren.cpp)
  set_property(TARGET libren PROPERTY PREFIX "")
  target_link_libraries(libren PUBLIC ren PRIVATE ren-internal)

  add_library(ren-hot-reload STATIC hot-reload.cpp)
  target_include_directories(ren-hot-reload PUBLIC ${REN_INCLUDE})
  target_link_libraries(ren-hot-reload PUBLIC glm::glm tiny_imageformat PRIVATE ren::core SDL3::SDL3)
  target_compile_features(ren-hot-reload PUBLIC cxx_std_23)

  target_compile_definitions(ren-hot-reload
    PUBLIC REN_HOT_RELOAD
    PRIVATE
    REN_LIB_DIR="$<TARGET_FILE_DIR:libren>"
    REN_LIB_NAME="$<TARGET_FILE_NAME:libren>"
    REN_LIB_PATH="$<TARGET_FILE:libren>"
  )

  add_dependencies(ren-hot-reload libren)
  add_library(ren::ren ALIAS ren-hot-reload)
else()
  add_library(ren::ren ALIAS ren)
endif()

if (REN_BUILD_IMGUI_PLUGIN)
  message(STATUS "Enable ImGui plugin")
  target_link_libraries(ren PRIVATE imgui::imgui)
  target_compile_definitions(ren PRIVATE REN_IMGUI)
endif()

add_executable(ren-process-shader process-shader.cpp)
target_link_libraries(ren-process-shader ren::core SPIRV-Headers)
add_executable(ren::process-shader ALIAS ren-process-shader)

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
  target_link_libraries(ren-bake-ibl ren-baking-internal Microsoft::DirectXTex KTX::ktx stb)
endif()

add_subdirectory(sh)

add_executable(test-sequences test-sequences.cpp)
target_link_libraries(test-sequences ren::core)
