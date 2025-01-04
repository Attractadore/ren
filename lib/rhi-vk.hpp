#pragma once
#if REN_RHI_VULKAN
#include "core/StdDef.hpp"

#include <vulkan/vulkan.h>
// Include after vulkan.h
#include <vk_mem_alloc.h>

struct VolkDeviceTable;

namespace ren::rhi {

enum class QueueFamily;

namespace vk {

struct Adapter {
  u32 index = -1;
};

auto get_queue_family_index(Adapter adapter, QueueFamily family) -> u32;

struct DeviceData;

using Device = const DeviceData *;

auto get_vk_device(Device device) -> VkDevice;

auto get_vma_allocator(Device device) -> VmaAllocator;

struct Queue {
  VkQueue handle = nullptr;
  const VolkDeviceTable *vk = nullptr;
};

struct Semaphore {
  VkSemaphore handle = nullptr;
};

struct Allocation {
  VmaAllocation handle = nullptr;
};

struct Buffer {
  VkBuffer handle = nullptr;
  Allocation allocation = {};
};

struct Image {
  VkImage handle = nullptr;
};

struct Surface {
  VkSurfaceKHR handle = nullptr;
};

struct SwapChainData;

using SwapChain = SwapChainData *;

} // namespace vk

using namespace vk;

} // namespace ren::rhi

#endif // REN_RHI_VULKAN
