set(REN_DIRECTX12_DEBUG
    TRUE
    CACHE BOOL "Enable DirectX 12 debug layer")

set(D3D12MA_DIR ${EXTERNAL_DIR}/D3D12MemoryAllocator)
set(D3D12MA_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/D3D12MemoryAllocator)
add_subdirectory(${D3D12MA_DIR} ${D3D12MA_BUILD_DIR})
# FIXME: Hack to work around old MinGW headers
set(D3D12MA_PATCH ${D3D12MA_BUILD_DIR}/D3D12MAPatch.h)
file(WRITE ${D3D12MA_PATCH} "
#define D3D12_HEAP_FLAG_HARDWARE_PROTECTED D3D12_HEAP_FLAGS(0x100)
#define D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH D3D12_HEAP_FLAGS(0x200)
#define D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS D3D12_HEAP_FLAGS(0x400)
")
target_precompile_headers(D3D12MemoryAllocator PRIVATE ${D3D12MA_PATCH})

add_library(ren-dx12 SHARED ren-dx12.cpp
  DirectX12CommandAllocator.cpp
  DirectX12Device.cpp
  DirectX12Swapchain.cpp
)
target_include_directories(ren-dx12 PUBLIC ${REN_INCLUDE_GE})
target_link_libraries(
  ren-dx12
  PUBLIC ren d3d12 dxgi dxguid
  PRIVATE Support D3D12MemoryAllocator)
if(REN_DIRECTX12_DEBUG)
  message(STATUS "Enable DirectX 12 debug layer")
  target_compile_definitions(ren-dx12 PRIVATE REN_DIRECTX12_DEBUG)
endif()
