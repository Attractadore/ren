set(REN_DIRECTX12_DEBUG TRUE CACHE BOOL "Enable DirectX 12 debug layer")

add_library(ren-dx12 SHARED ren-dx12.cpp
  DirectX12Device.cpp
  DirectX12Swapchain.cpp
)
target_include_directories(ren-dx12 PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(
  ren-dx12
  PUBLIC ren d3d12 dxgi d3dcompiler
  PRIVATE Support)
if (REN_DIRECTX12_DEBUG)
  message(STATUS "Enable DirectX 12 debug layer")
  target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_DEBUG)
endif()
