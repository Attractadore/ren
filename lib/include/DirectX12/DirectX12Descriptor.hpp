#pragma once
#include <d3d12.h>

namespace ren {
struct Descriptor {
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
};
} // namespace ren
