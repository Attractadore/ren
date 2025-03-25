add_library(tiny_imageformat INTERFACE)
target_include_directories(tiny_imageformat INTERFACE ${CMAKE_CURRENT_LIST_DIR}/tiny_imageformat/include ${REN_INCLUDE})

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
  target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/imgui ${CMAKE_CURRENT_LIST_DIR}/imgui/backends)
  target_link_libraries(imgui PRIVATE SDL2::SDL2)
  add_library(imgui::imgui ALIAS imgui)
endif()

add_subdirectory(tracy)

if (REN_BUILD_BAKING_TOOLS OR REN_BUILD_SHADER_COMPILER OR REN_BUILD_EXAMPLES)
  add_subdirectory(cxxopts)
endif()

if (REN_BUILD_SHADER_COMPILER)
  add_subdirectory(glslang)
endif()

add_subdirectory(glm)
add_subdirectory(fmt)
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
target_compile_features(DirectXTex PUBLIC cxx_std_11)
find_package(OpenMP)
if(TARGET OpenMP::OpenMP_CXX)
  target_link_libraries(DirectXTex PUBLIC OpenMP::OpenMP_CXX)
endif()
add_library(Microsoft::DirectXTex ALIAS DirectXTex)
