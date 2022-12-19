#pragma once
#include "DeleteQueue.hpp"

#include <d3d12.h>

#include <cstdint>

namespace ren {
class DirectX12Device;

using DirectX12QueueCustomDeleter = QueueCustomDeleter<DirectX12Device>;

struct DirectX12Texture {
  ID3D12Resource *resource;
};

using DirectX12DeleteQueue =
    DeleteQueue<DirectX12Device, DirectX12QueueCustomDeleter, DirectX12Texture,
                IUnknown *>;
} // namespace ren
