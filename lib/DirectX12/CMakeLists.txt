set(REN_DIRECTX12_DEBUG
    OFF
    CACHE BOOL "Enable DirectX 12 debug layer")
set(REN_DIRECTX12_DEBUG_CALLBACK
    OFF
    CACHE BOOL "Enable DirectX 12 debug callback")
set(REN_DIRECTX12_FORCE_WARP_DEVICE
    OFF
    CACHE BOOL "Force WARP device for DirectX 12")

add_subdirectory(hlsl)

add_library(D3D12 INTERFACE)
target_link_libraries(D3D12 INTERFACE d3d12 dxgi dxguid)

set(D3D12MA_DIR ${EXTERNAL_DIR}/D3D12MemoryAllocator)
add_library(D3D12MA STATIC D3D12MA.cpp)
target_include_directories(
  D3D12MA
  PUBLIC ${D3D12MA_DIR}/include ${REN_PRIVATE_INCLUDE_DIR}
  PRIVATE ${D3D12MA_DIR}/src)
target_link_libraries(D3D12MA PUBLIC D3D12)
target_compile_features(D3D12MA PUBLIC cxx_std_14)

add_library(
  ren-dx12 SHARED
  ren-dx12.cpp
  DirectX12CommandAllocator.cpp
  DirectX12CommandBuffer.cpp
  DirectX12CPUDescriptorPool.cpp
  DirectX12Device.cpp
  DirectX12RenderGraph.cpp
  DirectX12Swapchain.cpp)
target_include_directories(ren-dx12 PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(
  ren-dx12
  PUBLIC ren d3d12 dxgi dxguid
  PRIVATE Support D3D12MA FullScreenRectShader BlitToSwapchainShader)
if(REN_DIRECTX12_DEBUG)
  message(STATUS "Enable DirectX 12 debug layer")
  target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_DEBUG)
  if(REN_DIRECTX12_DEBUG_CALLBACK)
    message(STATUS "Enable DirectX 12 debug callback")
    target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_DEBUG_CALLBACK)
  endif()
endif()
if(REN_DIRECTX12_FORCE_WARP_DEVICE)
  message(STATUS "Force WARP device for DirectX 12")
  target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_FORCE_WARP_DEVICE)
endif()
target_compile_definitions(ren-dx12 PRIVATE NOMINMAX)

target_sources(
  ren-dx12
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${REN_INCLUDE}
         FILES
         ${REN_INCLUDE}/ren/ren-dx12.h
         ${REN_INCLUDE}/ren/ren-dx12.hpp)
set_target_properties(ren-dx12 PROPERTIES EXPORT_NAME "dx12")
add_library(ren::dx12 ALIAS ren-dx12)

include(GNUInstallDirs)
install(
  TARGETS ren-dx12
  EXPORT ${REN_DIRECTX12_TARGETS}
  FILE_SET HEADERS)
install(
  EXPORT ${REN_DIRECTX12_TARGETS}
  NAMESPACE ren::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
