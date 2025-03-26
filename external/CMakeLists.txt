add_subdirectory(boost-config)
add_subdirectory(boost-assert)
add_subdirectory(boost-move)
add_subdirectory(boost-intrusive)
add_subdirectory(boost-container)

add_subdirectory(glm)

add_subdirectory(fmt)

if (REN_BUILD_SHADER_COMPILER OR REN_BUILD_BAKING_TOOLS OR REN_BUILD_EXAMPLES)
  add_subdirectory(cxxopts)
endif()

add_subdirectory(tracy)

add_subdirectory(KTX-Software)
add_library(KTX::ktx ALIAS ktx)
add_library(KTX::ktx_read ALIAS ktx_read)

add_library(tiny_imageformat INTERFACE)
target_include_directories(tiny_imageformat INTERFACE tiny_imageformat/include ${REN_INCLUDE})

add_library(spirv-reflect SPIRV-Reflect/spirv_reflect.c)
target_include_directories(spirv-reflect PUBLIC SPIRV-Reflect)

add_subdirectory(Vulkan-Headers)
target_compile_definitions(Vulkan-Headers INTERFACE VK_NO_PROTOTYPES)

add_subdirectory(volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)

add_subdirectory(VulkanMemoryAllocator)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_subdirectory(SDL)
else()
  find_package(SDL2 REQUIRED GLOBAL)
endif()

if (REN_BUILD_IMGUI_PLUGIN)
  add_library(imgui
    imgui/backends/imgui_impl_sdl2.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
  )
  target_include_directories(imgui PUBLIC imgui imgui/backends)
  target_link_libraries(imgui PRIVATE SDL2::SDL2)
  add_library(imgui::imgui ALIAS imgui)
endif()

if (REN_BUILD_SHADER_COMPILER)
  add_subdirectory(glslang)
endif()

if (REN_BUILD_BAKING_TOOLS)
  add_subdirectory(DirectX-Headers)
  
  include(FetchContent)
  FetchContent_Declare(sal URL https://raw.githubusercontent.com/dotnet/runtime/main/src/coreclr/pal/inc/rt/sal.h DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(sal)
  
  add_library(DirectXMath INTERFACE)
  target_include_directories(DirectXMath INTERFACE DirectXMath/Inc ${sal_SOURCE_DIR})
  target_compile_features(DirectXMath INTERFACE cxx_std_11)
  add_library(Microsoft::DirectXMath ALIAS DirectXMath)
  
  add_library(DirectXTex
    DirectXTex/DirectXTex/BC.cpp
    DirectXTex/DirectXTex/BC4BC5.cpp
    DirectXTex/DirectXTex/BC6HBC7.cpp
    DirectXTex/DirectXTex/DirectXTexCompress.cpp
    DirectXTex/DirectXTex/DirectXTexConvert.cpp
    DirectXTex/DirectXTex/DirectXTexDDS.cpp
    DirectXTex/DirectXTex/DirectXTexHDR.cpp
    DirectXTex/DirectXTex/DirectXTexImage.cpp
    DirectXTex/DirectXTex/DirectXTexMipmaps.cpp
    DirectXTex/DirectXTex/DirectXTexMisc.cpp
    DirectXTex/DirectXTex/DirectXTexNormalMaps.cpp
    DirectXTex/DirectXTex/DirectXTexPMAlpha.cpp
    DirectXTex/DirectXTex/DirectXTexResize.cpp
    DirectXTex/DirectXTex/DirectXTexTGA.cpp
    DirectXTex/DirectXTex/DirectXTexUtil.cpp
  )
  target_include_directories(DirectXTex PUBLIC DirectXTex/DirectXTex)
  target_link_libraries(DirectXTex PUBLIC Microsoft::DirectXMath Microsoft::DirectX-Headers)
  find_package(OpenMP)
  if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(DirectXTex PUBLIC OpenMP::OpenMP_CXX)
  endif()
  target_compile_features(DirectXTex PUBLIC cxx_std_11)
  add_library(Microsoft::DirectXTex ALIAS DirectXTex)

  add_subdirectory(meshoptimizer)
  add_library(meshoptimizer::meshoptimizer ALIAS meshoptimizer)

  add_library(mikktspace MikkTSpace/mikktspace.c)
  target_include_directories(mikktspace PUBLIC MikkTSpace) 
  add_library(mikktspace::mikktspace ALIAS mikktspace)
endif()

if (REN_BUILD_EXAMPLES)
  add_subdirectory(assimp)

  add_library(tinygltf tinygltf/tiny_gltf.cc)
  target_include_directories(tinygltf
    PUBLIC tinygltf
    PRIVATE assimp/contrib/rapidjson/include/rapidjson assimp/contrib/stb
  )
  target_compile_definitions(tinygltf PUBLIC TINYGLTF_NO_STB_IMAGE_WRITE TINYGLTF_USE_CPP14 TINYGLTF_USE_RAPIDJSON)
  target_compile_features(tinygltf PUBLIC cxx_std_14)
endif()
