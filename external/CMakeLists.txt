add_subdirectory(BLAKE3/c)

add_subdirectory(glm)

add_subdirectory(fmt)

add_subdirectory(tracy)

add_subdirectory(KTX-Software)
add_library(KTX::ktx ALIAS ktx)
add_library(KTX::ktx_read ALIAS ktx_read)

add_library(tiny_imageformat INTERFACE)
target_include_directories(tiny_imageformat INTERFACE tiny_imageformat/include ${REN_INCLUDE})

add_subdirectory(Vulkan-Headers)
target_compile_definitions(Vulkan-Headers INTERFACE VK_NO_PROTOTYPES)
add_subdirectory(SPIRV-Headers)

add_subdirectory(volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)

add_subdirectory(VulkanMemoryAllocator)

find_package(SDL3 GLOBAL)
if (NOT SDL3_FOUND)
  if (REN_HOT_RELOAD)
    set(SDL_SHARED ON)
    set(SDL_STATIC OFF)
  else()
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
  endif()
  add_subdirectory(SDL)
endif()

if (REN_BUILD_IMGUI_PLUGIN)
  if (REN_HOT_RELOAD)
    add_library(imgui SHARED imgui.hpp imgui.cpp)
  else()
    add_library(imgui STATIC imgui.hpp imgui.cpp)
  endif()
  target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR} imgui)
  target_link_libraries(imgui PRIVATE SDL3::SDL3)
  add_library(imgui::imgui ALIAS imgui)

  include(GenerateExportHeader)
  generate_export_header(imgui)
endif()

set(slang_version 2025.14.3)
set(slangc "${CMAKE_CURRENT_LIST_DIR}/slang/${slang_version}/${CMAKE_HOST_SYSTEM_NAME}/bin/slangc${CMAKE_HOST_EXECUTABLE_SUFFIX}" PARENT_SCOPE)

if (REN_BUILD_BAKING_TOOLS OR REN_BUILD_EXAMPLES)
  add_library(stb_image stb_image.c)
  target_include_directories(stb_image PUBLIC stb)
endif()

if (REN_BUILD_BAKING_TOOLS)
  add_subdirectory(DirectX-Headers)
  
  include(FetchContent)
  FetchContent_Declare(sal URL https://raw.githubusercontent.com/dotnet/runtime/main/src/coreclr/pal/inc/rt/sal.h DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(sal)
  
  add_library(DirectXMath INTERFACE)
  target_include_directories(DirectXMath SYSTEM INTERFACE DirectXMath/Inc ${sal_SOURCE_DIR})
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
  target_include_directories(DirectXTex SYSTEM PUBLIC DirectXTex/DirectXTex)
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
