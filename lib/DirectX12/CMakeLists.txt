option(REN_DIRECTX12_DEBUG "Enable DirectX 12 debug layer" OFF)
option(REN_DIRECTX12_FORCE_WARP_DEVICE "Force WARP device for DirectX 12" OFF)

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
  ren-dx12
  ren-dx12.cpp
  DirectX12CommandAllocator.cpp
  DirectX12CommandBuffer.cpp
  DirectX12CPUDescriptorPool.cpp
  DirectX12Device.cpp
  DirectX12RenderGraph.cpp
  DirectX12Swapchain.cpp)
target_sources(ren-dx12 PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE} FILES
                               ${REN_INCLUDE}/ren/ren-dx12.h)
target_precompile_headers(
  ren-dx12
  PRIVATE
  <comdef.h>
  <d3d12.h>
  <d3d12sdklayers.h>
  <dxgi1_3.h>
  <dxgi1_4.h>
  <dxgidebug.h>
  <dxgiformat.h>
  <unknwn.h>
  <wrl/client.h>)
target_link_libraries(
  ren-dx12
  PUBLIC ren::ren d3d12 dxgi dxguid
  PRIVATE Support Support-PCH D3D12MA FullScreenRectShader
          BlitToSwapchainShader)
if(REN_DIRECTX12_DEBUG)
  message(STATUS "Enable DirectX 12 debug layer")
  target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_DEBUG)
endif()
if(REN_DIRECTX12_FORCE_WARP_DEVICE)
  message(STATUS "Force WARP device for DirectX 12")
  target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_FORCE_WARP_DEVICE)
endif()
target_compile_definitions(ren-dx12 PRIVATE NOMINMAX)

if(BUILD_SHARED_LIBS)
  set_target_properties(ren-dx12 PROPERTIES EXPORT_NAME "dx12")
  add_library(ren::dx12 ALIAS ren-dx12)
else()
  include(BundleDependencies)
  bundle_dependencies(ren-dx12 ren-dx12-bundled)
  set_target_properties(ren-dx12-bundled PROPERTIES EXPORT_NAME "dx12")
  add_library(ren::dx12 ALIAS ren-dx12-bundled)
endif()

add_library(ren-dx12-hpp INTERFACE)
target_sources(ren-dx12-hpp PUBLIC FILE_SET HEADERS BASE_DIRS ${REN_INCLUDE}
                                   FILES ${REN_INCLUDE}/ren/ren-dx12.hpp)
target_link_libraries(ren-dx12-hpp INTERFACE ren::ren-hpp ren::dx12)
set_target_properties(ren-dx12-hpp PROPERTIES EXPORT_NAME "dx12-hpp")
add_library(ren::dx12-hpp ALIAS ren-dx12-hpp)

if(REN_INSTALL)
  include(GNUInstallDirs)
  get_target_property(ren_dx12_target ren::dx12 ALIASED_TARGET)
  install(
    TARGETS ${ren_dx12_target} ren-dx12-hpp
    EXPORT ${REN_DIRECTX12_TARGETS}
    FILE_SET HEADERS)
  if(NOT BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PROPERTY:${ren_dx12_target},ARCHIVE_OUTPUT_NAME>
            TYPE LIB)
  endif()
  install(
    EXPORT ${REN_DIRECTX12_TARGETS}
    NAMESPACE ren::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
endif()
